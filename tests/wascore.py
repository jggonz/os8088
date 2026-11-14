#!/usr/bin/env python3
"""WireArena's deaths, respawns and score (docs/plans/WIREARENA-AI-PLAN.md 5).

One boot into the Heights arena at 40 columns, then, off the game's own
variables (its symbols from wirearena.asm assembled with a map, refused
unless byte-identical to the build) and its text screen:

  - A KILL: the pistol kills a bot - the player's kill, the bot's death;
  - DYING: a bot kills the player - the bot's kill, the player's death. While
    dead (3 s) W moves nothing and the trigger fires nothing, the eye sinks
    to the floor and "RESPAWN" counts on the screen; then the player is back
    at the respawn point farthest from the nearest live bot, whole, with the
    pistol alone;
  - A SUICIDE: the player's own rocket - a death, and a kill less;
  - A BOT'S RESPAWN: a bot killed comes back at the respawn point farthest
    from the player (on its route node);
  - THE SCOREBOARD: Tab held puts YOU and the three robots' names over the
    view; let go, it is gone;
  - THE WIDTH: - is 40 columns, + is 80.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import arenagen                                                 # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from waweapons import Arena, start, amap, SYS, APPS, FS, ITEM, A_, anims  # noqa

NAMES = ["COG", "RIVET", "SERVO", "SPROCKET", "DIODE", "PISTON", "BOLT",
         "GEAR"]


def main():
    need(APPS)
    sym = amap()
    A_.update(anims())
    boxes = [arenagen.bx(b) for b in arenagen.HEIGHTS_BOXES]
    nodes = arenagen.HEIGHTS_GRAPH["nodes"]
    grid = arenagen.sight_grid("a2", boxes)
    rsp = arenagen.respawns("a2", boxes, nodes, grid)
    fails = []

    def check(ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)

    shots = sys.argv[1] if len(sys.argv) > 1 else None
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

        def kd():
            return (list(g.p.read(sym["wa_kills"], 4)),
                    list(g.p.read(sym["wa_deaths"], 4)))

        def fig(i):
            return g.fig(i)

        def setb(i, off, data):
            g.p.write(sym["wa_figs"] + i * FS + off, data)

        def text(rows):
            d = m.read(0xB8000, 40 * 25 * 2)
            return ["".join(chr(d[(r * 40 + c) * 2]) for c in range(40))
                    for r in rows]

        def park(i):
            setb(i, 17, bytes([250]))
            setb(i, 0, struct.pack('<hh', 1000, 1900))

        g.tick(4)
        names = [NAMES[b] for b in g.p.read(sym["wa_bname"], 3)]
        check(len(set(names)) == 3, "three robots' names, all different: %r"
              % names)

        # A KILL: the pistol, bot 1 stood still (its fire held: a bench's)
        g.p.write(sym["wa_bdet"], b"\x01")
        setb(1, 10, bytes([0]))         # (no shield: a fifth of lives have one)
        park(0)                         # (the others out of the line of fire)
        park(2)
        n = 0
        while n < 4 and not fig(1)["dead"]:
            if n:
                g.tick(11)              # (the pistol's gap: WA_PCOOL)
            g.face(1, 300)
            g.press("ControlLeft")
            n += 1
        k, d = kd()
        check(fig(1)["dead"] and k[0] == 1 and d[2] == 1,
              "A KILL: the pistol kills bot 1 - kills %r, deaths %r" % (k, d))

        # A BOT'S RESPAWN: bot 1 back at the point farthest from the player
        g.p.write(sym["wa_bdet"], b"\x00")
        park(0)
        park(2)
        g.at(1024, 480, 0, ang=256)
        t0 = g.word("wa_tkn")
        gone = None
        for _ in range(150):
            g.tick(2)
            f = fig(1)
            if f["dead"] and not f["nfr"] and gone is None:
                gone = g.word("wa_tkn") - t0
            if not f["dead"]:
                break
        back = g.word("wa_tkn") - t0
        check(gone is not None and 80 <= gone <= 100 and 225 <= back <= 250,
              "A BOT'S RESPAWN: its body lies %r ticks (~90, 3 s), it is back "
              "after %d (~240, 8 s)" % (gone, back))
        f = fig(1)
        far = max(rsp, key=lambda r: abs(r[0] - 1024) + abs(r[1] - 480))
        nx, ny = nodes[far[3]]
        check(not f["dead"] and math.hypot(f["x"] - nx, f["y"] - ny) < 40,
              "A BOT'S RESPAWN: bot 1 back at (%d, %d), the node of the "
              "point farthest from the player %r" % (f["x"], f["y"], far))

        # DYING: bot 1 kills the player
        g.put("wa_diff", 2)
        g.put("wa_hp", 20)
        g.at(1000, 300, 0, ang=0)
        setb(1, 46, bytes([1, 0, 0]))
        setb(1, 55, bytes([0]))         # (its target: the player)
        dead = None
        for t in range(300):
            g.freeze(1, 1300, 300)
            g.tick(1)
            if g.byte("wa_pdead"):
                dead = t
                break
        g.tick(1)                       # (the death's own tick, finished)
        k, d = kd()
        check(dead is not None and d[0] == 1 and k[2] == 1,
              "DYING: bot 1 kills the player - kills %r, deaths %r" % (k, d))
        g.tick(2)
        x0, y0 = g.word("wa_px"), g.word("wa_py")
        mag0 = g.arms()["mag"][0]
        g.press("KeyW", ticks=6)
        g.press("ControlLeft")
        g.tick(10)
        eye = g.word("wa_eye") - g.word("wa_feet")
        rows = text(range(10, 16))
        check((g.word("wa_px"), g.word("wa_py")) == (x0, y0)
              and g.arms()["mag"][0] == mag0,
              "DYING: dead, W moves nothing and the trigger fires nothing")
        check(eye == 20 and any("RESPAWN" in r for r in rows),
              "DYING: the eye on the floor (%d) and RESPAWN on the screen "
              "(%r)" % (eye, [r for r in rows if "RESPAWN" in r]))
        bots = [(fig(i)["x"], fig(i)["y"]) for i in range(3)
                if not fig(i)["dead"]]
        for _ in range(60):
            g.tick(2)
            if not g.byte("wa_pdead"):
                break
        px, py = g.word("wa_px"), g.word("wa_py")
        s = g.arms()
        at = [r for r in rsp if (r[0], r[1]) == (px, py)]
        best = max(rsp, key=lambda r: min([abs(r[0] - bx_) + abs(r[1] - by_)
                                           for bx_, by_ in bots] or [99999]))
        check(at and s["hp"] == 100 and s["own"] == 1 and s["wpn"] == 0,
              "RESPAWN: back at a respawn point (%d, %d), whole, the pistol "
              "alone (%r)" % (px, py, s))
        check(at and at[0][:2] == best[:2],
              "RESPAWN: the point farthest from the nearest live bot %r (the "
              "bots at %r)" % (best[:2], bots))

        # A SUICIDE: the player's own rocket into a wall close by
        k0, d0 = kd()
        g.p.write(sym["wa_bdet"], b"\x01")
        g.p.write(sym["wa_own"], bytes([1 | 16]))
        g.p.write(sym["wa_mag"] + 4, bytes([3]))
        g.p.write(sym["wa_wpn"], bytes([4]))
        g.put("wa_hp", 10)
        g.at(1790, 1024, 0, ang=0)
        g.tick(8)
        g.press("ControlLeft")
        g.tick(10)
        k, d = kd()
        check(d[0] == d0[0] + 1 and k[0] == max(0, k0[0] - 1),
              "A SUICIDE: a death, and a kill less (%r -> %r, %r -> %r)"
              % (k0[0], k[0], d0[0], d[0]))
        for _ in range(60):
            g.tick(2)
            if not g.byte("wa_pdead"):
                break

        # THE SCOREBOARD: Tab held, then let go
        m.key("Tab", up=False)
        g.tick(6)
        held = text(range(6, 13))
        if shots:
            wd, ht, dd = m.fbuf(0)
            os88marty.write_png_rgb(shots, wd, ht, dd)
        m.key("Tab", down=False)
        g.tick(6)
        gone = text(range(6, 13))
        board = " ".join(held)
        check("NAME" in board and "YOU" in board
              and all(n in board for n in names),
              "THE SCOREBOARD: Tab held shows YOU and %r (%r)" % (names, held))
        check(not any("YOU" in r for r in gone), "THE SCOREBOARD: let go, "
              "it is gone")

        # THE WIDTH: - and +
        g.press("Equal")
        g.tick(4)
        w80 = g.byte("wa_mode")
        g.press("Minus")
        g.tick(4)
        w40 = g.byte("wa_mode")
        check(w80 == 0 and w40 == 1, "THE WIDTH: + is 80 columns (%d), - is "
              "40 (%d)" % (w80, w40))

    if fails:
        print("wascore: FAIL - %d of the score's checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wascore: ok - deaths, respawns, the score and its board")


if __name__ == "__main__":
    main()
