#!/usr/bin/env python3
"""WireArena's MODES, wave M1 (docs/plans/WIREARENA-MODES-PLAN.md 2, 3, 5):
the setup's lists by mode, Win at, team deathmatch and the rotation's next
arena, on one machine, off the game's own variables.

  - ARENAS BY MODE: with Mode at team deathmatch the Arena list holds the
    arenas with sides (the hedge, the Twin), and the arena moves off one
    that cannot play it (the heights, the default) to the first of them;
    Win at's items are the mode's; back at free-for-all, all five arenas
    in the setup's order (open first);
  - A FREE-FOR-ALL WIN: Win at 5 kills, a bot's fifth shows its name
    WINS; after the 10 s the match goes on in the next arena (pillars, after
    open), the scores anew and the bots' names kept; our fifth there shows
    YOU WIN, and the next is the heights;
  - TEAM DEATHMATCH: the teams' tops are their players' kills (us and bot
    2 red, bot 1 blue) - nine is no win at Win at 10, the tenth shows RED
    WINS; after the 10 s the Twin (next after the hedge), us on red's side.

Red with: wa_sarlist's mode test inverted (TDM offers the arenas it cannot
play), wc_score's team sum read off the wrong team, wc_next taking the
same arena again.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                               # noqa: E402
from os88fixture import need                                   # noqa: E402
from walift import amap, APPS, ITEM                            # noqa: E402
from waarenas import G, boot                                   # noqa: E402

F = 4772727
TICK = F // 30
DR_ITEMS, DR_N = 8, 10
FOC_MODE = 6


def main():
    need(APPS)
    fails = []

    def check(ok, what, got=""):
        if ok:
            print("  ok   %s" % what, flush=True)
        else:
            print("  FAIL %s %s" % (what, got), flush=True)
            fails.append(what)

    sym = amap()
    with boot() as ui:
        m = ui.m
        w = ui.path("B:/WARENA.O88")
        os88marty.until(m, lambda _: any(c[0] == "Game" and any(
            i[0] == ITEM for i in c[3]) for c in ui.menus()),
            "WireArena's Game menu on the bar", guest=10)
        p = os88marty.Pkg(m, w)
        g = G(ui, w, sym)

        def arenas():
            n = p.word(sym["wa_sdr_a"] + DR_N)
            return [p.byte(sym["wa_salx"] + i) for i in range(n)]

        def mode_key(key):
            p.write(sym["wa_sfoc"], bytes([FOC_MODE]))
            m.key(key)
            m.advance(cycles=TICK * 6)

        check(arenas() == [1, 0, 2, 3, 4] and
              p.byte(sym["wa_lkarena"]) == 2,
              "ARENAS BY MODE: free-for-all, all five, open first; the "
              "heights picked", (arenas(), p.byte(sym["wa_lkarena"])))
        mode_key("ArrowRight")                  # free-for-all -> TDM
        check(p.byte(sym["wa_lkgmode"]) == 2 and arenas() == [3, 4] and
              p.byte(sym["wa_lkarena"]) == 3,
              "ARENAS BY MODE: team deathmatch, the hedge and the Twin - the "
              "heights left for the hedge",
              (p.byte(sym["wa_lkgmode"]), arenas(),
               p.byte(sym["wa_lkarena"])))
        check(p.word(sym["wa_sdr_g"] + DR_ITEMS) == sym["wa_sg_tdm"],
              "WIN AT: team deathmatch's items")
        mode_key("ArrowLeft")                   # ...and back
        check(p.byte(sym["wa_lkgmode"]) == 0 and arenas() == [1, 0, 2, 3, 4]
              and p.word(sym["wa_sdr_g"] + DR_ITEMS) == sym["wa_sg_ffa"],
              "ARENAS BY MODE: free-for-all again, all five, its Win at")

        def play(arena, mode, goal):
            p.write(sym["wa_lkarena"], bytes([arena]))
            p.write(sym["wa_want"], bytes([1]))
            p.write(sym["wa_lkbots"], bytes([2]))
            p.write(sym["wa_lkgmode"], bytes([mode]))
            p.write(sym["wa_sgoal"] + mode, bytes([goal]))
            m.key("Enter")
            m.go()
            f0 = p.byte(sym["wa_frame"])
            os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                            "the match's first frames", guest=20)
            p.write(sym["wa_bdet"], b"\x01")    # (dummies: nobody's kills
            m.advance(cycles=TICK * 2)          # but ours)

        def kills(k):
            p.write(sym["wa_kills"], bytes(k))
            m.advance(cycles=TICK * 3)

        def text(a):
            b = bytearray()
            while p.byte(a + len(b)):
                b.append(p.byte(a + len(b)))
            return b.decode()

        def next_arena(want):
            m.go()
            os88marty.until(m, lambda _: p.byte(sym["wa_arena"]) == want and
                            g.word("wa_cwin") == 0, "arena %d next" % want,
                            guest=20)
            f0 = p.byte(sym["wa_frame"])
            os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                            "its frames", guest=10)

        play(1, 0, 1)                           # FREE-FOR-ALL, Win at 5
        check(p.byte(sym["wa_ggoal"]) == 5, "WIN AT: 5 kills",
              p.byte(sym["wa_ggoal"]))
        names = bytes(p.byte(sym["wa_bname"] + i) for i in range(3))
        kills([0, 0, 4, 0])
        check(g.word("wa_cwin") == 0, "A FREE-FOR-ALL WIN: four, no win")
        kills([0, 0, 5, 0])
        msg = text(p.word(sym["wa_cmsg"])) if g.word("wa_cwin") else ""
        check(g.word("wa_cwin") > 0 and msg.endswith(" WINS") and
              msg != "RED WINS", "A FREE-FOR-ALL WIN: bot 2's fifth shows "
              "its name WINS", msg)
        next_arena(0)
        check(bytes(p.byte(sym["wa_kills"] + i) for i in range(4)) ==
              bytes(4) and names == bytes(p.byte(sym["wa_bname"] + i)
                                          for i in range(3)),
              "THE NEXT ARENA: pillars after open, the kills anew, the bots' "
              "names kept")
        kills([5, 0, 0, 0])
        check(g.word("wa_cwin") > 0 and g.word("wa_cmsg") == sym["wc_m_youw"],
              "A FREE-FOR-ALL WIN: our fifth shows YOU WIN")
        next_arena(2)
        check(True, "THE NEXT ARENA: the heights after pillars")
        m.key("Escape")                         # (back to the setup)
        m.go()
        os88marty.until(m, lambda _: p.byte(sym["wa_quit"]) == 1,
                        "the match over", guest=5)
        m.advance(cycles=F * 2)

        play(3, 2, 1)                           # TEAM DEATHMATCH, Win at 10
        check(p.byte(sym["wa_gmode"]) == 2 and p.byte(sym["wa_ggoal"]) == 10,
              "TEAM DEATHMATCH: the mode, Win at 10",
              (p.byte(sym["wa_gmode"]), p.byte(sym["wa_ggoal"])))
        kills([4, 3, 5, 0])
        caps = (p.byte(sym["wa_caps"]), p.byte(sym["wa_caps"] + 1))
        check(caps == (9, 3) and g.word("wa_cwin") == 0,
              "TEAM DEATHMATCH: red 9 (us 4, bot 2 5), blue 3 - no win", caps)
        kills([4, 3, 6, 0])
        check(g.word("wa_cwin") > 0 and g.word("wa_cmsg") == sym["wc_m_rwin"],
              "TEAM DEATHMATCH: red's tenth shows RED WINS")
        next_arena(4)
        check(g.word("wa_px") < 1024 and p.byte(sym["wa_gmode"]) == 2,
              "THE NEXT ARENA: the Twin after the hedge, us on red's side",
              g.word("wa_px"))
    if fails:
        print("wamodes: FAIL - %d of the checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wamodes: ok - arenas by mode, Win at, the free-for-all and team "
          "deathmatch wins, the next arena")


if __name__ == "__main__":
    main()
