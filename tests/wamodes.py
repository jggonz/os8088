#!/usr/bin/env python3
"""WireArena's MODES, wave M1 (docs/plans/completed/WIREARENA-MODES-PLAN.md 2, 3, 5):
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
    YOU WIN, and the next is the crossing;
  - TEAM DEATHMATCH: the teams' tops are their players' kills (us and bot
    2 red, bot 1 blue) - nine is no win at Win at 10, the tenth shows RED
    WINS; after the 10 s the Twin (next after the hedge), us on red's side.

  - THE ROTATION (wave M3): the Arena list's last item is Rotation...; R
    opens its window with team deathmatch's arenas both checked; the Twin
    unchecked leaves it, the hedge (then the last) cannot be; Esc puts it
    away - and the team deathmatch win on the hedge goes on on the hedge;
  - KING OF THE HILL (wave M2): its arenas; on the hedge the hill taken by
    us, held when contested, taken by the foe alone, a point each 10 s held
    and the win; the next arenas round; the bots go for it.

Red with: wa_sarlist's mode test inverted (TDM offers the arenas it cannot
play), wc_score's team sum read off the wrong team, wc_next taking the
same arena again, wc_next ignoring the rotation, wc_hgoal returning at
once.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                               # noqa: E402
from os88fixture import need                                   # noqa: E402
from walift import amap, APPS, ITEM, MENU                            # noqa: E402
from waarenas import G, boot                                   # noqa: E402

F = 4772727
TICK = F // 30
DR_ITEMS, DR_N = 8, 10
WC_ST, WC_X, WC_Y, WC_Z, WC_TMR = 0, 2, 4, 6, 8
F_X, F_DEAD = 0, 17
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
        w = ui.path("B:/WIRERENA.O88")
        os88marty.until(m, lambda _: any(c[0] == MENU and any(
            i[0] == ITEM for i in c[3]) for c in ui.menus()),
            "WireArena's Link menu on the bar", guest=10)
        p = os88marty.Pkg(m, w)
        g = G(ui, w, sym)
        os88marty.until(m, lambda _: p.byte(sym["wa_sdsel"] + 7) != 0xFF,
                        "the setup drawn", guest=10)
        lev, vs, want = (p.byte(sym[n]) for n in ("wa_level", "wa_vsync",
                                                  "wa_want"))
        check((lev, vs) == ((2, 0) if want else (4, 1)) and
              p.byte(sym["wa_sdsel"] + 7) == lev,
              "THE DETAIL BY THE CPU'S TIER: Medium on an 8088 (40 columns), "
              "Very High and its vsync on a 286 or better (80), and the "
              "setup's Detail drop-down showing it",
              (lev, vs, want, p.byte(sym["wa_sdsel"] + 7)))

        def arenas():                           # (the list's last item is
            n = p.word(sym["wa_sdr_a"] + DR_N)  # Rotation..., 0FFh)
            a = [p.byte(sym["wa_salx"] + i) for i in range(n)]
            return a[:-1] if a and a[-1] == 0xFF else a + ["no rotation"]

        def mode_key(key):              # (the mode's change itself waited
            g0 = p.byte(sym["wa_lkgmode"])  # for, not six ticks: on Bochs
            p.write(sym["wa_sfoc"], bytes([FOC_MODE]))  # the key landed
            m.key(key)                  # later than that, on the build
            m.go()                      # before Detail too, and the row
            os88marty.until(m, lambda _: p.byte(sym["wa_lkgmode"]) != g0,
                            "the mode's change (%s)" % key, guest=5)
            m.advance(cycles=TICK * 2)  # read Team Deathmatch still)

        check(arenas() == [1, 0, 5, 2, 3, 4, 7, 6] and
              p.byte(sym["wa_lkarena"]) == 2,
              "ARENAS BY MODE: free-for-all, all eight, open first; the "
              "heights picked", (arenas(), p.byte(sym["wa_lkarena"])))
        mode_key("ArrowRight")                  # free-for-all -> TDM
        check(p.byte(sym["wa_lkgmode"]) == 2 and
              arenas() == [5, 3, 4, 7, 6] and
              p.byte(sym["wa_lkarena"]) == 5,
              "ARENAS BY MODE: team deathmatch, the arenas with sides - the "
              "crossing, the hedge, the Twin, the pit, the ziggurat - the "
              "heights left for the crossing",
              (p.byte(sym["wa_lkgmode"]), arenas(),
               p.byte(sym["wa_lkarena"])))
        check(p.word(sym["wa_sdr_g"] + DR_ITEMS) == sym["wa_sg_tdm"],
              "WIN AT: team deathmatch's items")
        mode_key("ArrowLeft")                   # ...and back
        check(p.byte(sym["wa_lkgmode"]) == 0 and arenas() == [1, 0, 5, 2, 3, 4, 7, 6]
              and p.word(sym["wa_sdr_g"] + DR_ITEMS) == sym["wa_sg_ffa"],
              "ARENAS BY MODE: free-for-all again, all eight, its Win at")

        def play(arena, mode, goal, bots=2):
            p.write(sym["wa_lkarena"], bytes([arena]))
            p.write(sym["wa_want"], bytes([1]))
            p.write(sym["wa_lkbots"], bytes([bots]))
            p.write(sym["wa_lkgmode"], bytes([mode]))
            p.write(sym["wa_sgoal"] + mode, bytes([goal]))
            m.key("Enter")
            m.go()
            f0 = p.byte(sym["wa_frame"])
            os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                            "the match's first frames", guest=20)
            f0 = p.byte(sym["wa_frame"])        # (wa_reset moves the count:
            os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                            "a frame of the loop's", guest=5)   # the loop's)
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
        next_arena(5)
        check(True, "THE NEXT ARENA: the crossing after pillars")
        m.key("Escape")                         # (back to the setup)
        m.go()
        os88marty.until(m, lambda _: p.byte(sym["wa_quit"]) == 1,
                        "the match over", guest=5)
        m.advance(cycles=F * 2)

        mode_key("ArrowRight")                  # THE ROTATION (M3): team
        m.key("KeyR")                           # deathmatch's, its window
        m.go()
        os88marty.until(m, lambda _: p.word(sym["wa_rwin"]) != 0 and
                        p.byte(sym["wa_rck"] + 10) == 1, "the rotation "
                        "window painted", guest=5)
        ck = sym["wa_rck"]

        def on(i):
            return p.byte(ck + i * 12 + 10)
        check(p.word(sym["wa_rwin"]) != 0 and p.byte(sym["wa_rn"]) == 5 and
              all(on(i) == 1 for i in range(5)),
              "THE ROTATION: R opens its window - the five arenas, all "
              "checked", (p.word(sym["wa_rwin"]), p.byte(sym["wa_rn"])))
        m.key("Space")                          # (the crossing, the focus's)
        for _ in range(4):                      # (the Twin, the pit, the
            m.key("ArrowDown")                  # ziggurat)
            if _:
                m.key("Space")
        m.advance(cycles=TICK * 6)
        rot = p.byte(sym["wa_srot"] + 2)
        check([on(i) for i in range(5)] == [0, 1, 0, 0, 0] and
              rot & (32 | 16 | 128 | 64) == 0 and rot & 8,
              "THE ROTATION: all but the hedge unchecked, out of it",
              ([on(i) for i in range(5)], rot))
        for _ in range(3):
            m.key("ArrowUp")
        m.key("Space")
        m.advance(cycles=TICK * 6)
        rot = p.byte(sym["wa_srot"] + 2)
        check(on(1) == 1 and rot & 8, "THE ROTATION: the last one checked "
              "(the hedge) stays checked", (on(1), rot))
        m.key("Escape")
        m.advance(cycles=TICK * 6)
        check(p.word(sym["wa_rwin"]) == 0, "THE ROTATION: Esc puts it away")

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
        next_arena(3)
        check(g.word("wa_px") < 1024 and p.byte(sym["wa_gmode"]) == 2 and
              p.byte(sym["wa_caps"]) == 0 and
              bytes(p.byte(sym["wa_kills"] + i) for i in range(4)) ==
              bytes(4),
              "THE NEXT ARENA: the hedge again, the Twin out of the rotation "
              "- the scores anew, us on red's side", g.word("wa_px"))
        m.key("Escape")
        m.go()
        os88marty.until(m, lambda _: p.byte(sym["wa_quit"]) == 1,
                        "the match over", guest=5)
        m.advance(cycles=F * 2)

        p.write(sym["wa_lkgmode"], b"\x01")    # KING OF THE HILL (M2): the
        mode_key("ArrowRight")                  # list's fourth, after CTF
        check(p.byte(sym["wa_lkgmode"]) == 3 and
              arenas() == [5, 2, 3, 4, 7, 6],
              "ARENAS BY MODE: king of the hill, the crossing, the heights, "
              "the hedge, the pit, the ziggurat and "
              "the Twin", (p.byte(sym["wa_lkgmode"]), arenas()))
        play(3, 3, 1, bots=1)                   # the hedge, Win at 3
        hill = (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y),
                p.word(sym["wa_cfl"] + WC_Z))
        check(p.byte(sym["wa_gmode"]) == 3 and p.byte(sym["wa_gside"]) == 1
              and hill == (1024, 832, 0) and
              p.byte(sym["wa_cfl"] + WC_ST) == 0,
              "KING OF THE HILL: the mode, the hedge's sides, its hill at the "
              "stack's foot, nobody's", (p.byte(sym["wa_gmode"]),
                                         p.byte(sym["wa_gside"]), hill))
        b1 = sym["wa_figs"]

        def foe_on(x, y, n):                    # (bot 1, blue, held there)
            for _ in range(n):
                p.write(b1 + F_X, struct.pack('<hh', x, y))
                p.write(b1 + F_DEAD, b"\x00")
                m.advance(cycles=TICK)

        foe_on(1824, 1152, 2)
        g.place(hill[0], hill[1], 0)
        m.advance(cycles=TICK * 3)
        check(p.byte(sym["wa_cfl"] + WC_ST) == 1 and
              p.word(sym["wa_cmsg"]) == sym["wc_m_hwe"],
              "THE HILL: ours to run over - red's, WE HAVE THE HILL",
              p.byte(sym["wa_cfl"] + WC_ST))
        foe_on(hill[0], hill[1], 3)             # both on it: as it was
        check(p.byte(sym["wa_cfl"] + WC_ST) == 1,
              "THE HILL: both teams on it, it stays red's")
        g.place(224, 1152, 0)
        foe_on(hill[0], hill[1], 3)
        check(p.byte(sym["wa_cfl"] + WC_ST) == 2 and
              p.word(sym["wa_cmsg"]) == sym["wc_m_hthey"],
              "THE HILL: the foe alone on it, blue's - THEY HAVE THE HILL")
        foe_on(1824, 1152, 1)
        p.write(sym["wa_cfl"] + WC_TMR, struct.pack('<H', 295))
        m.advance(cycles=TICK * 8)
        check((p.byte(sym["wa_caps"]), p.byte(sym["wa_caps"] + 1)) == (0, 1),
              "THE HILL: held 10 s, blue's point",
              (p.byte(sym["wa_caps"]), p.byte(sym["wa_caps"] + 1)))
        p.write(sym["wa_caps"], bytes([0, 2]))
        p.write(sym["wa_cfl"] + WC_TMR, struct.pack('<H', 295))
        m.advance(cycles=TICK * 8)
        check(g.word("wa_cwin") > 0 and g.word("wa_cmsg") == sym["wc_m_bwin"],
              "THE HILL: blue's third point, BLUE WINS")
        next_arena(4)
        check(p.byte(sym["wa_gside"]) == 1 and g.word("wa_px") < 1024 and
              (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y)) ==
              (1024, 832) and p.byte(sym["wa_cfl"] + WC_ST) == 0,
              "THE NEXT ARENA: the Twin after the hedge - sides, us on red's, "
              "its hill nobody's", g.word("wa_px"))
        def hill_win():                         # (red's, and its third
            p.write(sym["wa_cfl"] + WC_ST, b"\x01")  # point: the next)
            p.write(sym["wa_caps"], bytes([2, 0]))
            p.write(sym["wa_cfl"] + WC_TMR, struct.pack('<H', 295))
            m.advance(cycles=TICK * 8)
        hill_win()
        next_arena(7)                           # (the pit, its centre)
        hill = (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y),
                p.word(sym["wa_cfl"] + WC_Z))
        check(hill == (1024, 1024, 0), "THE NEXT ARENA: the pit after the "
              "Twin, its hill at the pit's centre", hill)
        hill_win()
        next_arena(6)                           # (the ziggurat, its top)
        hill = (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y),
                p.word(sym["wa_cfl"] + WC_Z))
        check(hill == (1024, 1024, 288), "THE NEXT ARENA: the ziggurat "
              "after the pit, its hill on the tower's top", hill)
        hill_win()
        next_arena(5)                           # (round: the crossing)
        hill = (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y),
                p.word(sym["wa_cfl"] + WC_Z))
        check(p.byte(sym["wa_gside"]) == 1 and hill == (1024, 1024, 0),
              "THE NEXT ARENA: the crossing after the ziggurat, round - sides, "
              "its hill at the centre", (p.byte(sym["wa_gside"]), hill))
        hill_win()
        next_arena(2)
        hill = (p.word(sym["wa_cfl"] + WC_X), p.word(sym["wa_cfl"] + WC_Y),
                p.word(sym["wa_cfl"] + WC_Z))
        check(p.byte(sym["wa_gside"]) == 0 and hill == (1248, 1024, 80),
              "THE NEXT ARENA: the heights after the crossing - no sides, "
              "the hill on its platform", (p.byte(sym["wa_gside"]), hill))
        m.key("Escape")
        m.go()
        os88marty.until(m, lambda _: p.byte(sym["wa_quit"]) == 1,
                        "the match over", guest=5)
        m.advance(cycles=F * 2)

        p.write(sym["wa_lkarena"], b"\x03")    # THE BOTS PLAY FOR IT: the
        p.write(sym["wa_lkbots"], b"\x02")      # hedge, us idle, bot 2 red's
        p.write(sym["wa_sgoal"] + 3, b"\x00")   # and bot 1 blue's, no limit
        m.key("Enter")
        m.go()
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                        "the match's first frames", guest=20)
        took, t = set(), 0                      # (the hedge's hill is off
        while t < 480 and took != {1, 2}:       # the bots' patrols: with no
            g.put("wa_hp", 100)                 # goal it stood a minute
            m.advance(cycles=F // 4)            # untaken)
            t += 1
            st = p.byte(sym["wa_cfl"] + WC_ST)
            if st:
                took.add(st)
        check(took == {1, 2}, "THE BOTS PLAY: each team's bot takes the "
              "hill within 2 minutes (%d s)" % (t // 4), took)
    if fails:
        print("wamodes: FAIL - %d of the checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wamodes: ok - arenas by mode, Win at, the free-for-all and team "
          "deathmatch wins, the next arena")


if __name__ == "__main__":
    main()
