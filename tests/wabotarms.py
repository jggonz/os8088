#!/usr/bin/env python3
"""WireArena's bots arm themselves and use each weapon (docs/plans/
completed/WIREARENA-AI-PLAN.md 3.3, wave B4).

One boot into the Heights arena at 40 columns, then, off the game's own
variables (its symbols from wirearena.asm assembled with a map, refused
unless byte-identical to the build):

  - A WEAPON TAKEN: a bot reaching the ramp platform's spawn node takes what
    is there (the grenade launcher, written in): its weapon, a pickup's
    rounds, and the spawn empty;
  - HEALTH FETCHED: a roaming bot at 30 health FETCHES the health pack - its
    goal the pack's node - goes there, takes it (100) and the pack is gone;
  - A ROCKET: a bot with the rocket launcher, engaged, fires a real rocket,
    owned by it; the player is hurt, the bot's own splash spares it, and
    the kill when it comes is the bot's;
  - THE HITSCANS: the chaingun fires at its own short gap, a railgun hit is
    100, a close shotgun hit 60.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import arenagen                                                 # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from waweapons import Arena, start, amap, SYS, APPS, FS, ITEM, A_, anims, RKGSZ  # noqa

F_BST, F_BWPN, F_BGOAL, F_BAMMO = 46, 50, 51, 54
GESZ = 12


def main():
    need(APPS)
    sym = amap()
    A_.update(anims())
    boxes = [arenagen.bx(b) for b in arenagen.HEIGHTS_BOXES]
    nodes, edges, homes = arenagen.graph_edges("a2", boxes)
    fails = []

    def check(ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        p = start(ui, w, sym)
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                        "the Heights arena's first frames", guest=20)
        m.pause()
        g = Arena(ui, w, sym)

        def fb(i, off, n=1):
            return g.p.read(sym["wa_figs"] + i * FS + off, n)

        def setb(i, off, data):
            g.p.write(sym["wa_figs"] + i * FS + off, data)

        def park(i):
            setb(i, 17, bytes([250]))
            setb(i, 0, struct.pack('<hh', 1000, 1900))

        def alive():
            """the player alive again, the respawn landed whole (its tick
            zeroes the timer an instruction before it restores the player)"""
            for _ in range(80):
                if not g.byte("wa_pdead") and g.word("wa_hp") > 0:
                    break
                g.tick(2)
            g.tick(2)

        def arriving(i, e, z=0):
            """bot i on edge e, a tick from its far node (written again until
            it reads back so: a write that lands inside the bot's own tick
            can be lost)"""
            for _ in range(4):
                arrive1(i, e, z)
                if struct.unpack('<H', fb(i, 6, 2))[0] == e * GESZ:
                    return

        def arrive1(i, e, z):
            a, b_ = edges[e][0], edges[e][1]
            (x1, y1) = nodes[b_]
            setb(i, 6, struct.pack('<H', e * GESZ))
            setb(i, 0, struct.pack('<hh', x1 - (x1 - nodes[a][0]) // 50,
                                   y1 - (y1 - nodes[a][1]) // 50))
            setb(i, 8, struct.pack('<H', 1))
            setb(i, 18, struct.pack('<h', z))
            setb(i, 11, bytes([A_["RUN"], 8, 1]))
            setb(i, 16, bytes([0, 0]))
            setb(i, F_BST, bytes([0]))
            setb(i, 45, bytes([0]))         # (not waiting for the lift)
            setb(i, F_BWPN, bytes([0]))     # (the pistol alone)

        g.at(1024, 200, 0, ang=768)     # (south, out of everyone's way)
        g.put("wa_hp", 30000)
        park(1)
        park(2)

        # A WEAPON TAKEN: bot 0 arriving at node 1, the platform's spawn
        g.respawn(2, 2)                 # (its kind: the grenade launcher)
        g.at(1024, 200, 0, ang=768)
        e01 = next(i for i, e in enumerate(edges) if e[0] == 0 and e[1] == 1)
        arriving(0, e01, 80)
        g.tick(4)
        wpn, ammo = fb(0, F_BWPN)[0], fb(0, F_BAMMO)[0]
        check(wpn == 3 and ammo == 8 and not g.byte("wa_pkon") & 4,
              "A WEAPON TAKEN: at the spawn, the grenade launcher (%d), 8 "
              "rounds (%d), the spawn empty" % (wpn, ammo))

        # HEALTH FETCHED: bot 0 at 30, arriving at node 6 (beside the pack's)
        park(0)
        g.tick(2)
        setb(0, 17, bytes([0]))
        e56 = next(i for i, e in enumerate(edges) if e[0] == 5 and e[1] == 6)
        arriving(0, e56)
        setb(0, 42, bytes([30]))
        setb(0, 10, bytes([0]))
        g.tick(3)
        st, goal = fb(0, F_BST)[0], fb(0, F_BGOAL)[0]
        took = False
        for _ in range(120):
            g.tick(2)
            if fb(0, 42)[0] == 100:
                took = True
                break
        check(st == 3 and goal == 7 and took and not g.byte("wa_pkon") & 1,
              "HEALTH FETCHED: FETCH (%d) for node %d, the pack taken (100) "
              "and gone" % (st, goal))

        # A ROCKET: bot 0 with the launcher, engaged, the player 400 off
        g.put("wa_hp", 30000)
        g.at(1000, 300, 0, ang=0)
        g.freeze(0, 1400, 300)
        setb(0, 4, struct.pack('<h', 512))
        setb(0, F_BST, bytes([1, 0, 0]))
        setb(0, 55, bytes([0]))         # (its target: the player)
        setb(0, F_BWPN, bytes([4]))
        setb(0, F_BAMMO, bytes([6]))
        setb(0, 10, bytes([0]))
        setb(0, 42, bytes([100]))
        own = None
        for t in range(80):
            g.freeze(0, 1400, 300)
            park(1)
            park(2)
            g.tick(1)
            for k in range(4):
                st_ = g.p.read(sym["wa_rks"] + 8 * k + 6, 1)[0]
                if st_ == 1:
                    own = g.p.read(sym["wa_rkg"] + RKGSZ * k + 14, 1)[0]
            if g.word("wa_hp") < 30000:
                break
        hurt = g.word("wa_hp") < 30000
        check(own == 1 and hurt, "A ROCKET: bot 0 fires one, its owner %r, "
              "and the player is hurt (%s)" % (own, hurt))
        # ...its splash spares it: the player beside it, the rocket's blast
        g.put("wa_hp", 30000)
        g.at(1320, 300, 0, ang=0)       # (80 units off: the bot in its reach)
        hp0 = fb(0, 42)[0]
        for t in range(60):
            g.freeze(0, 1400, 300)
            setb(0, F_BAMMO, bytes([6]))
            park(1)
            park(2)
            g.tick(1)
        check(fb(0, 42)[0] == hp0 and not fb(0, 17)[0],
              "A ROCKET: its own splash spares the bot (%d -> %d)"
              % (hp0, fb(0, 42)[0]))
        # ...and its kill is its own
        k0 = list(g.p.read(sym["wa_kills"], 4))
        g.at(1000, 300, 0, ang=0)
        g.put("wa_hp", 30)
        for t in range(150):
            g.freeze(0, 1400, 300)
            setb(0, F_BAMMO, bytes([6]))
            park(1)
            park(2)
            g.tick(1)
            if g.byte("wa_pdead"):
                break
        g.tick(2)
        k1 = list(g.p.read(sym["wa_kills"], 4))
        check(g.byte("wa_pdead") and k1[1] == k0[1] + 1,
              "A ROCKET: the player killed, the kill bot 0's (%r -> %r)"
              % (k0, k1))
        alive()

        # THE HITSCANS: the chaingun's gap; a railgun's and a shotgun's hit
        def duel(wpn, dist, ticks):
            alive()
            g.put("wa_hp", 30000)
            g.at(1000, 300, 0, ang=0)
            g.freeze(0, 1000 + dist, 300)
            setb(0, 4, struct.pack('<h', 512))
            setb(0, F_BST, bytes([1, 0, 0]))
            setb(0, 55, bytes([0]))         # (its target: the player)
            setb(0, F_BWPN, bytes([wpn]))
            g.put("wa_bshots", 0)
            drops = []
            last = g.word("wa_hp")
            for t in range(ticks):
                g.freeze(0, 1000 + dist, 300)
                setb(0, F_BAMMO, bytes([60]))
                park(1)
                park(2)
                g.tick(1)
                hp = g.word("wa_hp")
                if hp < last:
                    drops.append(last - hp)
                last = hp
            return g.word("wa_bshots"), drops
        shots, _ = duel(2, 400, 150)
        check(shots >= 25, "THE HITSCANS: the chaingun, %d rounds in 150 "
              "ticks (its gap 4)" % shots)
        _, drops = duel(5, 400, 200)
        check(drops and set(drops) == {100}, "THE HITSCANS: a railgun hit "
              "is 100 (%r)" % drops)
        _, drops = duel(1, 200, 200)
        check(drops and set(drops) == {60}, "THE HITSCANS: a shotgun hit at "
              "3 m is 60 (%r)" % drops)

    if fails:
        print("wabotarms: FAIL - %d of the bots' arms' checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wabotarms: ok - the bots arm themselves and use their arms")


if __name__ == "__main__":
    main()
