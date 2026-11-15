#!/usr/bin/env python3
"""WireArena's CAPTURE THE FLAG, wave C1 (docs/plans/WIREARENA-CTF-PLAN.md
2, 10): the rules on one machine, off the game's own variables, on the Twin.

  - THE MODE AND THE TEAMS: the setup's Mode at CTF makes a CTF match; the
    teams are the slots' parity (us red, bot 1 blue, bot 2 red); the flags
    stand at their bases;
  - SIDE SPAWNS: at the start, and after each of three deaths, we stand on
    red's side (the Twin's west square, x < 1024);
  - A TAKE: standing at blue's base we carry blue's flag;
  - NO CAPTURE AWAY: at our base with our own flag away (poked carried by
    the foe, who stands at our base with it) nothing scores; touching our
    own flag, poked dropped, RETURNS it;
  - A CAPTURE: then at our base, red has 1 and blue's flag is home;
  - A DROP: carrying, we die - blue's flag lies where we fell, and goes home
    by itself when its time runs out;
  - THE WIN (Win at 3, its default): at 2 captures, a third shows RED
    WINS, and after its 10 s the match goes on in the next CTF arena of the
    rotation, the pit - nobody's captures, both flags home at its bases,
    us on our side (WIREARENA-MODES-PLAN 5);
  - NO FRIENDLY FIRE: our pistol at our teammate (bot 2) takes nothing off
    it; at the foe (bot 1) it does;
  - THE BOTS PLAY (wave C2, CTF-PLAN 6): two bots and us idle - bot 2
    red's, bot 1 blue's - and within six guest minutes each has carried
    the other's flag, and two captures have been made. (Who captures first
    is the bots' dice: one bot's capture alone is not asked for.)

Red with: the touch's team test inverted (we take our own flag, a capture
never comes), the capture's home test dropped (a capture with our own flag
away), wa_hurt's teammate guard taken out (bot 2 hurt), wc_bgoal
returning at once (the bots roam: no capture in four minutes).
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
F_SIZE, F_X, F_Y, F_DEAD, F_HP = 56, 0, 2, 17, 42
WC_SZ, WC_ST, WC_CARR, WC_X, WC_Y, WC_TMR = 10, 0, 1, 2, 4, 8
HOME, CAR, DROP = 0, 1, 2
RED, BLUE = (192, 1024), (1856, 1024)
PIT_RED = (64, 1024)            # (on the ring, 240 up)


def match(ui, arena, bots):
    sym = amap()
    w = ui.path("B:/WARENA.O88")
    m = ui.m
    os88marty.until(m, lambda _: any(c[0] == MENU and any(
        i[0] == ITEM for i in c[3]) for c in ui.menus()),
        "WireArena's Link menu on the bar", guest=10)
    p = os88marty.Pkg(m, w)
    p.write(sym["wa_lkarena"], bytes([arena]))
    p.write(sym["wa_want"], bytes([1]))
    p.write(sym["wa_lkbots"], bytes([bots]))
    p.write(sym["wa_lkgmode"], bytes([1]))
    m.key("Enter")
    f0 = p.byte(sym["wa_frame"])
    os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                    "the match's first frames", guest=20)
    f0 = p.byte(sym["wa_frame"])                # (a frame of the loop's:
    os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,  # the mode
                    "a frame of the loop's", guest=5)            # is set)
    return G(ui, w, sym)


class C(G):
    def flag(self, k, off, size=1):
        a = self.sym["wa_cfl"] + k * WC_SZ + off
        return self.p.byte(a) if size == 1 else self.p.word(a)

    def setflag(self, k, st, x, y):
        a = self.sym["wa_cfl"] + k * WC_SZ
        self.p.write(a + WC_ST, bytes([st, 0]))
        self.p.write(a + WC_X, struct.pack('<hhh', x, y, 0))
        self.p.write(a + WC_TMR, struct.pack('<H', 900))

    def caps(self):
        return (self.p.byte(self.sym["wa_caps"]),
                self.p.byte(self.sym["wa_caps"] + 1))

    def fig(self, i, off, size=2):
        a = self.sym["wa_figs"] + i * F_SIZE + off
        return self.p.byte(a) if size == 1 else self.word("wa_figs",
                                                          i * F_SIZE + off)

    def ticks(self, n):
        self.m.advance(cycles=TICK * n)


def main():
    need(APPS)
    fails = []

    def check(ok, what, got=""):
        if ok:
            print("  ok   %s" % what, flush=True)
        else:
            print("  FAIL %s %s" % (what, got), flush=True)
            fails.append(what)

    with boot() as ui:
        g = match(ui, 4, 2)
        g.__class__ = C
        g.p.write(g.sym["wa_bdet"], b"\x01")    # (dummies: neither shoot
        sym = g.sym                             # nor hunt)
        check(g.p.byte(sym["wa_gmode"]) == 1, "THE MODE: CTF")
        tm = [g.p.byte(sym["wa_tm"] + i) for i in range(3)]
        check(tm == [0, 1, 0], "THE TEAMS: us red, bot 1 blue, bot 2 red",
              tm)
        check((g.flag(0, WC_X, 2), g.flag(0, WC_Y, 2)) == RED and
              (g.flag(1, WC_X, 2), g.flag(1, WC_Y, 2)) == BLUE and
              g.flag(0, WC_ST) == HOME and g.flag(1, WC_ST) == HOME,
              "THE FLAGS: both at their bases")
        xs = [g.word("wa_px")]
        for _ in range(3):                      # three deaths, three spawns
            g.p.write(sym["wa_hp"], struct.pack('<H', 0))
            g.p.write(sym["wa_pdead"], bytes([2]))
            g.ticks(10)
            xs.append(g.word("wa_px"))
        check(all(x < 1024 for x in xs), "SIDE SPAWNS: on red's side, at "
              "the start and three respawns", xs)
        # THE WEAPON'S SPRITE UNDER CTF'S LINES: the overlay holds the flag
        # line before the weapon's cells, and the sprite's own cells are
        # made from none - not from where the overlay stood. Begun there,
        # the shotgun came out 5 cells of 12 and wa_spn counted cells that
        # were never its, past its table under a long message: stray blocks
        # mid-screen after a respawn (the owner, 2026-11-15)
        spn = []
        for wpn in (1, 0):                      # the shotgun, then the pistol
            g.p.write(sym["wa_wpn"], bytes([wpn]))
            g.p.write(sym["wa_ovd"], b"\x01")
            g.ticks(4)
            spn.append(g.word("wa_spn"))
            if wpn == 1:
                vr = bytes(g.m.read(0xB8000, 40 * 24 * 2))
                brown = sum(1 for k in range(0, len(vr), 2)
                            if vr[k + 1] == 0x06 and vr[k] != 0x20)
        check(spn == [12, 7] and brown == 8, "THE WEAPON'S SPRITE, the "
              "flag line before it in the overlay: the shotgun's 12 cells "
              "(its stock's 8 brown ones on the glass), the pistol's 7",
              (spn, brown))

        g.place(BLUE[0], BLUE[1], 0)            # A TAKE
        g.ticks(4)
        check(g.flag(1, WC_ST) == CAR and g.flag(1, WC_CARR) == 0,
              "A TAKE: blue's flag ours to carry",
              (g.flag(1, WC_ST), g.flag(1, WC_CARR)))
        # NO CAPTURE AWAY: our flag carried by the foe (bot 1), who stands
        # at OUR base with it - we come home with theirs, and nothing scores
        g.setflag(0, CAR, RED[0], RED[1])
        g.p.write(sym["wa_cfl"] + WC_CARR, bytes([1]))
        b1 = sym["wa_figs"]
        g.place(RED[0] + 8, RED[1], 0)
        for _ in range(4):
            g.p.write(b1 + F_X, struct.pack('<hh', RED[0] - 8, RED[1]))
            g.p.write(b1 + F_DEAD, b"\x00")
            g.ticks(1)
        check(g.caps() == (0, 0) and g.flag(1, WC_ST) == CAR,
              "NO CAPTURE AWAY: our flag carried off, nothing scores",
              (g.caps(), g.flag(1, WC_ST)))
        g.p.write(b1 + F_X, struct.pack('<hh', 1700, 1250))  # (the foe off
        g.setflag(0, DROP, 640, 800)            # A RETURN, by our touch -
        #                                       # our base, or it takes our
        #                                       # flag again, as it may)
        g.place(640, 800, 0)
        g.ticks(4)
        check(g.flag(0, WC_ST) == HOME and (g.flag(0, WC_X, 2),
                                            g.flag(0, WC_Y, 2)) == RED,
              "A RETURN: our dropped flag touched goes home")
        g.place(RED[0], RED[1], 0)              # A CAPTURE
        g.ticks(4)
        check(g.caps() == (1, 0) and g.flag(1, WC_ST) == HOME,
              "A CAPTURE: red 1, blue's flag home", (g.caps(),
                                                     g.flag(1, WC_ST)))

        g.place(BLUE[0], BLUE[1], 0)            # A DROP
        g.ticks(4)
        g.place(1300, 1200, 0)
        g.ticks(2)
        g.p.write(sym["wa_hp"], struct.pack('<H', 0))
        g.p.write(sym["wa_pdead"], bytes([90]))
        g.ticks(2)
        check(g.flag(1, WC_ST) == DROP and abs(g.flag(1, WC_X, 2) - 1300)
              < 16 and abs(g.flag(1, WC_Y, 2) - 1200) < 16,
              "A DROP: blue's flag where we fell",
              (g.flag(1, WC_ST), g.flag(1, WC_X, 2), g.flag(1, WC_Y, 2)))
        g.p.write(sym["wa_cfl"] + WC_SZ + WC_TMR, struct.pack('<H', 5))
        g.ticks(8)
        check(g.flag(1, WC_ST) == HOME, "A DROP: home by itself when its "
              "time runs out", g.flag(1, WC_ST))
        g.ticks(100)                            # (alive again)

        def shoot_at(i):                        # NO FRIENDLY FIRE: figure i
            base = sym["wa_figs"] + i * F_SIZE   # stood 120 ahead of us in
            def stand():                        # the west square, every tick
                g.p.write(base + F_X, struct.pack('<hh', 420, 800))  # of the
                g.p.write(base + F_DEAD, b"\x00")                   # shot
            g.p.write(base + 10, b"\x00")       # (F_SH: no shield to take it)
            g.p.write(base + F_HP, bytes([100]))
            g.place(300, 800, 0, ang=0)
            for _ in range(30):
                stand()
                g.ticks(1)                      # (the last shot's cooldown
            hp0 = g.fig(i, F_HP, 1)             # over; drawn there: the shot
            mag0 = g.p.byte(sym["wa_mag"])      # traces what was drawn)
            g.m.key("ControlLeft", up=False)
            for _ in range(6):
                stand()
                g.ticks(1)
            g.m.key("ControlLeft", down=False)
            g.ticks(2)
            return (hp0, g.fig(i, F_HP, 1), g.fig(i, F_DEAD, 1),
                    mag0 - g.p.byte(sym["wa_mag"]))
        g.p.write(sym["wa_pkon"], b"\x00")      # (no pickup in the way)
        mate = shoot_at(1)
        foe = shoot_at(0)
        check(mate[3] == 1 and mate[1] == mate[0] and mate[2] == 0,
              "NO FRIENDLY FIRE: a shot fired at our teammate (bot 2) takes "
              "nothing", mate)
        check(foe[3] == 1 and (foe[1] < foe[0] or foe[2] != 0),
              "...and the same shot at the foe (bot 1) hurts it", foe)
        g.p.write(sym["wa_caps"], bytes([2, 0]))  # THE WIN (Win at 3,
        g.place(BLUE[0], BLUE[1], 0)            # the default)
        g.ticks(4)
        g.place(RED[0], RED[1], 0)
        g.ticks(4)
        check(g.caps() == (3, 0) and g.word("wa_cwin") > 0 and
              g.word("wa_cmsg") == sym["wc_m_rwin"],
              "THE WIN: red's third capture shows RED WINS",
              (g.caps(), g.word("wa_cwin")))
        g.m.go()
        os88marty.until(g.m, lambda _: g.p.byte(sym["wa_arena"]) == 7 and
                        g.word("wa_cwin") == 0, "the pit next", guest=20)
        f0 = g.p.byte(sym["wa_frame"])          # (wa_arena is set before
        os88marty.until(g.m, lambda _: g.p.byte(sym["wa_frame"]) != f0,
                        "its frames", guest=10)  # the read: a frame of the
        g.ticks(4)                              # loop's is the match on)
        check(g.caps() == (0, 0) and g.flag(0, WC_ST) == HOME and
              g.flag(1, WC_ST) == HOME and (g.flag(0, WC_X, 2),
                                            g.flag(0, WC_Y, 2)) == PIT_RED
              and g.word("wa_px") < 1024,
              "THE WIN: after its 10 s the next CTF arena, the pit - no "
              "captures, both flags home at its bases, us on our side",
              (g.caps(), g.word("wa_px")))
    with boot() as ui:                          # C2: THE BOTS PLAY FOR IT
        g = match(ui, 4, 2)                     # (CTF-PLAN 6) - us idle,
        g.__class__ = C                         # bot 2 red's, bot 1 blue's:
        sym = g.sym                             # one against one
        took, caps, last, t = [0, 0], 0, g.caps(), 0
        while t < 360 and not (all(took) and caps >= 2):
            g.p.write(sym["wa_hp"], struct.pack('<H', 100))
            g.ticks(30)
            t += 1
            for k in (0, 1):                    # (flag k carried: the other
                if g.flag(k, WC_ST) == CAR and g.flag(k, WC_CARR) != 0:
                    took[1 - k] += 1            # team's bot has it)
            c = g.caps()
            caps += sum(max(0, c[k] - last[k]) for k in (0, 1))
            last = c
        check(all(took) and caps >= 2, "THE BOTS PLAY: each team's bot "
              "takes the other's flag, two captures in all, within 6 "
              "minutes (red took it %d s, blue %d s; %d captures; %d s)"
              % (took[0], took[1], caps, t))
    if fails:
        print("wactf: FAIL - %d of the checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("wactf: ok - the mode, the teams, side spawns, a take, no capture "
          "away, a return, a capture, a drop and its time, the win and the "
          "round, no friendly fire")


if __name__ == "__main__":
    main()
