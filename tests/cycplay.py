#!/usr/bin/env python3
"""Cyclone's play fixes, on the machine (SPEC.md 67.23).

  1. a kill scores what cy_kindsc SAYS it scores - the table is words and was
     indexed by a byte, so a tanker paid 25,600 instead of 100;
  2. the superzapper is recharged at the top of every level and says so;
  3. ...and says something DIFFERENT when it is fired;
  4. CYP_LIFE exists and grants a life.

THE SUPERZAPPER IS THE INSTRUMENT for (1), and that is what makes the row
cheap: it calls cy_score_kind once per live enemy, so a board holding exactly
one enemy turns one keystroke into one score award that can be read straight
out of [cy_score]. No play, no aiming, no waiting for a wave.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): this row IS the break. Taking
`shl bx, 1` back out of cy_score_kind - the defect as shipped - reads
tanker 25,600 against 100 and fuseball 12,800 against 250, with flipper still
correct, which is the shape that let it live: the FIRST entry of a word table
indexed by a byte is right, so a glance at a flipper kill says nothing is
wrong.
"""
import sys, os
# THIS TREE's root, DERIVED - never a hard-coded path (tests/cycweb.py's note)
_R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_R, "tools"))
sys.path.insert(0, os.path.join(_R, "tests"))
import os88marty, os88mouse, os88sym, os88geom, dispcp
from cycweb import pkg_syms, Pkg, u16, CYS_TITLE, CYS_PLAY

KINDS = [("flipper", 0, 150), ("tanker", 1, 100), ("spiker", 2, 50),
         ("fuseball", 3, 250), ("pulsar", 4, 200)]


def main():
    S = os88sym.linear
    bad = 0
    with os88marty.launch(os.path.join(_R, "build/os8088-360.img"),
                          apps=os.path.join(_R, "build/apps360.img"),
                          machine="os8088_5150_herc_gla", boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        entry = dispcp.row_of(m, S, "CYCLONE.O88")
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        os88marty.until(m, lambda _m: any(
            w.title.startswith("Cyclone") for w in os88geom.windows(m, S)),
            "the Cyclone window", poll=0.5, limit=60)
        seg = [u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
               for w in os88geom.windows(m, S) if w.title.startswith("Cyclone")][0]
        syms, image = pkg_syms()
        # ...AND CHECK THE PACKAGE IS THIS SOURCE, which is cycweb's guard and
        # is here for the reason it bit twice while this row was written: a
        # knob build left in build/ (CYPROF=1) moves every symbol, so the row
        # reads plausible rubbish and fails ninety seconds later complaining
        # about the warp. A range of CODE, not the whole image - the header is
        # stamped by os88pkg.py and cy_ekcol is patched for the display.
        lo, n = syms["cy_entry"], 2048
        live = bytes(m.read(seg * 16 + lo, n))
        if live != image[lo:lo + n]:
            raise RuntimeError("the CYCLONE.O88 running here is not this "
                               "source - run `make` (a knob build in build/ "
                               "is the usual cause)")
        p = Pkg(m, seg, syms)
        mo.to(2, 2)
        for _ in range(10):
            if p.rb("cy_state") != CYS_TITLE:
                break
            m.key("Enter")
            try:                        # a key the title missed is pressed
                os88marty.until(m, lambda _m: p.rb("cy_state") != CYS_TITLE,
                                "the title to take Enter", poll=0.2,
                                guest=9.0)          # ...again
            except os88marty.MartyError:
                pass
        os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_PLAY,
                        "the warp to finish", poll=0.5, limit=90)

        # --- 2. the level-start recharge ----------------------------------
        z = p.rb("cy_zap")
        print("after the level-1 warp: cy_zap = %d (want 1)" % z)
        bad += (z != 1)

        # --- 1. what a kill is worth. The superzapper is the instrument: it
        # calls cy_score_kind for every live enemy, so one enemy is one award.
        m.write(p.addr("cy_spawn_tick"), bytes([0xC3]))
        for name, kind, want in KINDS:
            for i in range(10):                 # an empty board...
                m.write(p.addr("cy_e_kind") + i, bytes([0xFF]))
            m.write(p.addr("cy_e_kind"), bytes([kind]))   # ...but one
            m.write(p.addr("cy_e_lane"), bytes([0]))
            m.write(p.addr("cy_e_dp"), (0x0800).to_bytes(2, "little"))
            p.ww("cy_left", 1)
            p.ww("cy_wleft", 40)
            p.ww("cy_score", 0)
            p.ww("cy_score + 2", 0) if False else \
                m.write(p.addr("cy_score") + 2, b"\x00\x00")
            p.wb("cy_zap", 1)
            m.key("KeyZ")                        # fire the superzapper
            # cy_superzap spends the charge FIRST, then scores and says so;
            # the charge going is the key landing, the quiesce its finish
            os88marty.until(m, lambda _m: p.rb("cy_zap") == 0,
                            "the superzapper to fire", poll=0.1, guest=30.0)
            os88marty.quiesce(m, lambda: (m.read(p.addr("cy_score"), 4),
                                          m.read(p.addr("cy_msgs"), 2)),
                              guest=0.3, what="the zapper's award")
            lo = u16(m.read(p.addr("cy_score"), 2))
            hi = u16(m.read(p.addr("cy_score") + 2, 2))
            got = lo | (hi << 16)
            ok = got == want                     # level 1 -> multiplier 1
            print("  %-9s scored %6d   want %4d   %s"
                  % (name, got, want, "ok" if ok else "FAIL"))
            bad += not ok
            if kind == 0:                        # ...and the FIRED line
                msg = u16(m.read(p.addr("cy_msgs"), 2))
                fired = syms["cy_s_zapfired"]
                print("  zapper fired says +%d (cy_s_zapfired is +%d)  %s"
                      % (msg, fired, "ok" if msg == fired else "FAIL"))
                bad += (msg != fired)
        # --- 5. A DEATH DOES NOT RESTART THE WAVE (SPEC.md 67.25) ---------
        # cy_die_update called cy_wavesize, which sets cy_wleft to the FULL
        # wave for the level - so every death put the still-to-spawn count back
        # to 40 from level 13 on, and a player who died twice could play for a
        # long time and never reach the end of the level.
        p.wb("cy_lives", 3)
        p.ww("cy_wleft", 7)             # ...7 still to come
        p.ww("cy_left", 2)              # ...and 2 on the web
        p.wb("cy_state", 4)             # CYS_DIE
        p.ww("cy_dietim", 1)            # ...expiring now
        m.advance(cycles=3 * 262000)
        wl, lf, lv = p.rw("cy_wleft"), p.rw("cy_left"), p.rb("cy_lives")
        ok = wl == 7 and lf == 0 and lv == 2
        print("  after a death: wleft %d (want 7), left %d (want 0), "
              "lives %d (want 2)   %s" % (wl, lf, lv, "ok" if ok else "FAIL"))
        bad += not ok

        print("\ncycplay: %d failing check(s)" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
