#!/usr/bin/env python3
"""WireArena's bots see, face and shoot back (docs/plans/completed/WIREARENA-AI-PLAN.md
4): fast more than good.

One boot into the Heights arena at 40 columns (the difficulty first set from
the Game menu), then, off the game's own variables (its symbols from
wirearena.asm assembled with a map, refused unless byte-identical to the
build):

  - THE SETUP: Skill hard (the setup's drop-down, from the keyboard)
    makes the difficulty 2;
  - SIGHT: a bot stood at 40 random free points, the player at another each
    time: its verdict (F_BSEE) is the host's walk of the same sight grid
    (arenagen.sight_clear) every time;
  - THE DUEL, at each difficulty: a bot stood 400 units from the player,
    still, for 600 ticks. It turns to face the player (within ~7 degrees),
    fires at its gap (the shots within 20% of 600 / its gap), shows its cue
    (F_FLASH's yellow bit) and hits at its accuracy (within 15 points of
    45, 60 and 75%); each hit flashes the HUD's heart (wa_phit);
  - THE HUNT: a bot that sees the player, and then does not (the player
    taken out of its sight), hunts the node nearest where it saw them, gets
    there and roams again;
  - BOT AGAINST BOT: the player out of sight, two bots face to face pick
    each other as targets (the owner's play note: alternate looks, player
    and bots), fight, and one kills the other - the kill and the death a
    bot's, the player unhurt.

A bot that never turns, never fires, or hits at a wrong rate is red, and so
is a sight walk that disagrees with the host's.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import arenagen                                                 # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from waweapons import Arena, start, amap, SYS, APPS, FS, ITEM, MENU, A_, anims  # noqa

F_BST, F_BSEE, F_BGOAL, F_BTGT = 46, 49, 51, 55


def main():
    need(APPS)
    sym = amap()
    A_.update(anims())
    boxes = [arenagen.bx(b) for b in arenagen.HEIGHTS_BOXES]
    top = arenagen.hmax(boxes)
    tall = [b for b in boxes if b.h >= top]
    grid = arenagen.sight_grid("a2", boxes)
    nodes = arenagen.HEIGHTS_GRAPH["nodes"]
    fails = []

    def check(ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WARENA.O88")
        os88marty.until(m, lambda _: any(c[0] == MENU and any(
            i[0] == ITEM for i in c[3]) for c in ui.menus()),
            "WireArena's Link menu on the bar", guest=10)
        for k in ("ArrowDown", "ArrowDown", "ArrowDown", "ArrowRight"):
            m.key(k)                    # (the setup's Skill: normal -> hard)
        ui.settle()
        p = os88marty.Pkg(m, w)
        check(p.byte(sym["wa_diff"]) == 2, "THE SETUP: Skill hard sets the "
              "difficulty (%d)" % p.byte(sym["wa_diff"]))
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

        def out_of_play(i):
            """bot i far away and dead for a while, out of the way"""
            setb(i, 17, bytes([250]))
            setb(i, 0, struct.pack('<hh', 1000, 1900))

        def place(i, x, y, ang=None, st=None):
            g.freeze(i, x, y)
            if ang is not None:
                setb(i, 4, struct.pack('<h', ang))
            if st is not None:
                setb(i, F_BST, bytes([st]))
                setb(i, F_BTGT, bytes([0]))     # (its target: the player)

        def free(rnd):
            while True:
                x, y = rnd.randint(40, 2008), rnd.randint(40, 2008)
                if not any(b[0] - 24 < x < b[2] + 24 and b[1] - 24 < y < b[3] + 24
                           for b in boxes if b.h > 0):
                    return x, y

        g.put("wa_hp", 30000)
        out_of_play(1)
        out_of_play(2)

        # SIGHT: bot 0 engaged (no field-of-view test), the player elsewhere
        rnd = random.Random(7)
        agree, n = 0, 40
        for _ in range(n):
            bx_, by_ = free(rnd)
            px, py = free(rnd)
            g.put("wa_hp", 30000)
            g.at(px, py, 0, ang=0)
            place(0, bx_, by_, st=1)
            out_of_play(1)              # (the others kept dead: a look at
            out_of_play(2)              # one of them is not this check's)
            g.tick(10)                  # (it looks every 8th tick)
            seen = fb(0, F_BSEE)[0] & 1
            want = arenagen.sight_clear(grid, (bx_, by_), (px, py))
            agree += (bool(seen) == want)
        check(agree == n, "SIGHT: the bot's look agrees with the host's walk "
              "of the same grid at %d of %d placements" % (agree, n))

        # THE DUEL, each difficulty
        for diff, gap, acc in ((0, 30, 45), (1, 20, 60), (2, 14, 75)):
            g.put("wa_diff", diff)
            g.put("wa_hp", 30000)
            g.at(1000, 300, 0, ang=0)
            place(0, 1400, 300, ang=0, st=1)    # engaged, facing away: it
            setb(0, 47, bytes([0, 0]))          # must turn (no reaction left)
            g.put("wa_bshots", 0)
            g.put("wa_bhits", 0)
            g.put("wa_seed", 0x5A17)    # (20-40 rolls: the clock's seed let
                                        # one run in ~200 land outside 15
                                        # points - pinned, the same each run)
            cue = flash = False
            angs = []
            for t in range(300):
                g.tick(2)
                if t % 5 == 0:
                    place(0, 1400, 300)
                    out_of_play(1)
                    out_of_play(2)
                g.put("wa_hp", 30000)
                if fb(0, 16)[0] & 0x80:
                    cue = True
                if g.byte("wa_phit"):
                    flash = True
                if t > 40:
                    angs.append(struct.unpack('<h', fb(0, 4, 2))[0] & 1023)
            shots, hits = g.word("wa_bshots"), g.word("wa_bhits")
            rate = 100.0 * hits / max(1, shots)
            off = max(min(abs(a - 512), 1024 - abs(a - 512)) for a in angs)
            check(off * 360 / 1024 < 8,
                  "THE DUEL (%d): it turns to face the player (worst %.1f "
                  "degrees off once on it)" % (diff, off * 360 / 1024))
            want = 600.0 / (gap + 1)          # (its gap counts on target)
            check(abs(shots - want) <= 0.2 * want + 2 and cue,
                  "THE DUEL (%d): %d shots in 600 ticks (gap %d: ~%.0f), its "
                  "yellow cue %s" % (diff, shots, gap, want,
                                     "seen" if cue else "NOT seen"))
            check(abs(rate - acc) <= 15 and flash,
                  "THE DUEL (%d): %d hits of %d, %.0f%% (its accuracy %d%%), "
                  "the heart flashed %s" % (diff, hits, shots, rate, acc,
                                            "yes" if flash else "NO"))

        # THE HUNT: seen at (1000, 300), then the player gone behind the annex
        g.put("wa_diff", 1)
        g.at(1000, 300, 0, ang=0)
        place(0, 1400, 300, ang=512, st=0)
        for _ in range(20):             # (ROAMING it looks every other turn
            g.tick(2)                   # of its own, 16 ticks apart, and at
            place(0, 1400, 300)         # the player every second look: 32
            out_of_play(1)              # ticks at most - MULTIPLAYER-FPS-PLAN
            out_of_play(2)              # 16.10; 12 held it while every turn
            if fb(0, F_BST)[0] == 1:    # was a look)
                break
        st0 = fb(0, F_BST)[0]
        g.at(64, 1024, 0, ang=0)                # (the back tunnel)
        hunted = goal = None
        back = False
        for t in range(300):
            g.tick(2)
            st = fb(0, F_BST)[0]
            if st == 2 and hunted is None:
                hunted = t
                goal = fb(0, F_BGOAL)[0]
            if hunted is not None and st == 0:
                back = True
                break
        x, y = struct.unpack('<hh', fb(0, 0, 4))
        nx, ny = nodes[goal] if goal is not None and goal < len(nodes) \
            else (0, 0)
        check(st0 == 1 and hunted is not None and back
              and math.hypot(x - nx, y - ny) < 150,
              "THE HUNT: engaged (%d), lost it, hunted node %r (%r) and, "
              "there (%d, %d), roams again (%s)"
              % (st0, goal, (nx, ny), x, y, back))

        # BOT AGAINST BOT: the player out of everyone's sight, bots 0 and 1
        # face to face in the open. They pick each other, fight, and one
        # kills the other - a bot's kill and a bot's death, the player's
        # health and deaths untouched
        g.put("wa_diff", 2)
        g.put("wa_hp", 30000)
        g.at(64, 1024, 0, ang=0)
        out_of_play(2)
        k0 = list(g.p.read(sym["wa_kills"], 4))
        d0 = list(g.p.read(sym["wa_deaths"], 4))
        for i, (x, a) in enumerate(((1000, 0), (1400, 512))):
            setb(i, 17, bytes([0]))             # (alive, whole)
            setb(i, 10, bytes([0]))
            setb(i, 42, bytes([100]))
            setb(i, 50, bytes([0]))             # (the pistol)
            place(i, x, 300, ang=a, st=0)
        mutual = False
        dead = None
        for t in range(400):
            g.tick(2)
            if t % 5 == 0:
                for i, x in ((0, 1000), (1, 1400)):
                    if not fb(i, 17)[0]:
                        g.freeze(i, x, 300)
                out_of_play(2)
            if (fb(0, F_BST)[0], fb(0, F_BTGT)[0], fb(1, F_BST)[0],
                    fb(1, F_BTGT)[0]) == (1, 2, 1, 1):
                mutual = True
            if fb(0, 17)[0] or fb(1, 17)[0]:
                dead = 0 if fb(0, 17)[0] else 1
                break
        g.tick(1)
        k1 = list(g.p.read(sym["wa_kills"], 4))
        d1 = list(g.p.read(sym["wa_deaths"], 4))
        check(mutual, "BOT AGAINST BOT: each picks the other (engaged, "
              "targets 2 and 1)")
        check(dead is not None and k1[2 - dead] == k0[2 - dead] + 1
              and d1[1 + dead] == d0[1 + dead] + 1,
              "BOT AGAINST BOT: bot %r killed, by the other - kills %r -> "
              "%r, deaths %r -> %r" % (dead, k0, k1, d0, d1))
        check(g.word("wa_hp") == 30000 and d1[0] == d0[0],
              "BOT AGAINST BOT: the player unhurt (%d), no death of theirs"
              % g.word("wa_hp"))

        # SHIELDS: a fifth of a bot's lives on normal, none on easy
        def lives(diff, n):
            g.put("wa_diff", diff)
            k = 0
            for _ in range(n):
                setb(2, 17, bytes([1]))
                g.tick(1)
                while fb(2, 17)[0]:
                    g.tick(1)
                k += fb(2, 10)[0] == 100
            return k
        on, off = lives(1, 80), lives(0, 20)
        check(6 <= on <= 28 and off == 0,
              "SHIELDS: %d of 80 lives shielded on normal (~16), %d of 20 on "
              "easy" % (on, off))

        def hold(st0=None):
            """bots 0 and 1 where they stand, whole, the player too"""
            g.put("wa_hp", 30000)
            for i, (x, y) in enumerate(spots):
                if not fb(i, 17)[0]:
                    g.freeze(i, x, y)
                    setb(i, 42, bytes([250]))
            out_of_play(2)

        # SHOOTS BACK: the player hits bot 0 from behind; it turns on them,
        # with a grudge of 90 ticks
        g.put("wa_diff", 1)
        out_of_play(1)
        out_of_play(2)
        spots = [(1100, 300)]
        g.at(700, 300, 0, ang=0)
        place(0, 1100, 300, ang=0, st=0)
        setb(0, 17, bytes([0, 0]))
        setb(0, 10, bytes([0]))
        setb(0, 42, bytes([250]))
        g.tick(4)                       # (drawn there: the shot traces the
        place(0, 1100, 300, ang=0, st=0)  # figures as the last frame drew them)
        g.tick(1)
        g.press("ControlLeft")
        grudge = g.p.read(sym["wa_grudge"], 1)[0]
        check((fb(0, F_BST)[0], fb(0, F_BTGT)[0]) == (1, 0) and grudge > 70,
              "SHOOTS BACK: shot from behind, bot 0 turns on the player "
              "(state %d, target %d), a grudge of %d ticks"
              % (fb(0, F_BST)[0], fb(0, F_BTGT)[0], grudge))
        # THE GRUDGE: bot 1 comes up close - bot 0 stays on the player while
        # its grudge runs, then takes the nearer enemy
        spots = [(1100, 300), (1250, 300)]
        setb(1, 17, bytes([0, 0]))
        setb(1, 10, bytes([0]))
        place(1, 1250, 300, ang=512, st=0)
        held, switched = g.p.read(sym["wa_grudge"], 1)[0] > 60, None
        for t in range(120):
            g.tick(2)
            if t % 5 == 0:
                hold()
            gr = g.p.read(sym["wa_grudge"], 1)[0]
            tg = fb(0, F_BTGT)[0]
            if gr > 4 and tg != 0:
                held = False
            if gr == 0 and tg == 2 and switched is None:
                switched = t
                break
        check(held and switched is not None,
              "THE GRUDGE: bot 0 on the player while it runs (%s), then on "
              "the nearer bot 1 (%r ticks in)" % (held, switched))

        # HEARING: the player fires at nothing; bot 0, roaming 500 off and
        # facing away, comes hunting them; bot 1, far down the back tunnel,
        # does not
        out_of_play(2)
        spots = [(1500, 300), (64, 1024)]
        g.at(1000, 300, 0, ang=256)
        for i, (x, y) in enumerate(spots):
            setb(i, 17, bytes([0, 0]))
            place(i, x, y, ang=0, st=0)
        g.tick(2)
        g.m.key("ControlLeft", up=False)    # (read as the shot is heard, not
        for _ in range(8):                  # after a press's five ticks: a
            g.tick(1)                       # hunting bot's next look may be
            if fb(0, F_BST)[0]:             # its turn at the OTHER bot, and
                break                       # its target then says so)
        st = (fb(0, F_BST)[0], fb(0, F_BTGT)[0], fb(1, F_BST)[0])
        g.m.key("ControlLeft", down=False)
        g.tick(1)
        check(st[0] in (1, 2) and st[1] == 0 and st[2] == 0,
              "HEARING: bot 0 within earshot hunts the player (state %d, "
              "target %d), bot 1 out of it roams on (%d)" % st)

    if fails:
        print("wabots: FAIL - %d of the bots' checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wabots: ok - the bots see, face, shoot and hunt")


if __name__ == "__main__":
    main()
