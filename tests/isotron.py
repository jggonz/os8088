#!/usr/bin/env python3
"""isoTron's rules on the real 8088 against the host model (SPEC.md 108.3 - 108.7).

    make isotrondisk && python3 tests/isotron.py --rules [--all] [--shot DIR]

RULES (the isotronrules soak row). One boot of MartyPC's genuine 5150 with a
CGA (os8088_5150_cga_gla, the machine the pace is fitted to). ISOTRON.O88 is
opened off drive B:, and for each tier the harness plays the first stored
game of tools/isotronsim.py's selfcheck - a seeded game whose bot player
covers a tower 1-2-3-4-1 and ends in three losses in a row - with it_tflags
FAST | BOT | NOFLOW | HOLD: iterations without the clock, the harness's own
player, no pauses between rounds, and a stop at every round's end. After
EVERY round the guest's own variables must equal the model's record:

    the height it was played at, the winner, the iterations, both scores, the
    tower afterwards, the losses in a row, the RNG's state, the placement
    checksum (rol(ck, 1) XOR idx over every cell, in order) and the head-steps

so a single decision, a single cell or a single RNG byte that differs fails
the row at the round it happened in. The game must then end (it_gover) and
return to the title. --all plays all six stored games. Then a SCRIPTED game
(tools/isotronsim.py SCRIPT: F10s, turns and F9 at fixed iterations, no bot)
goes through it_tinput the same way: it_script POSTS each entry into the UI
task's mailbox (it_kboost, it_kf9, the turn FIFO) exactly as it_key posts a
keystroke, so the worker's own consumer (it_gather's .keys) takes it - the
mailbox's consumer half, at iterations the model can name.

LAST, THE KEYBOARD ITSELF (no FAST, no BOT, no NOFLOW - HOLD only): real
keystrokes through MartyPC's keyboard into W_ONKEY, it_key and it_turn_ok -
the producer half, which no scripted game reaches. The worker is held with
it_tpause (it_commands still runs, it_flow does not), so what each key posted
can be read before any iteration takes it: 1 at the title starts a Rookie
game; P pauses it and a Del while paused is REFUSED; Space resumes; Del then
three Ins fill the 3-deep FIFO and drop the newest; F10 F10 F9 post two
boosts and a normal. Released, one iteration takes them (speed 2 + 2 = 4/6)
and three player cells take the turns - right, left, left: a DOUBLE-TAP
U-TURN, so the player ends one row up (d = 28) heading W. Then the flow
between rounds: a Space pressed during CRASH is KEPT and WAIT takes it with no
second press; Esc ends the game; and a game whose losses in a row stand at 2
goes CRASH -> GAME OVER -> the title on its own.

THE NEGATIVE CONTROL is the model's own: the same seed with the brief's tower
rule ("every win raises") must DISAGREE with what the guest played, or the
row could not tell the two rules apart and would prove nothing about which
one the guest has.

--shot DIR keeps one CGA capture of the window after the first game (for a
human to look at the wave-1 debug view; nothing is asserted on it).

Numbers are guest state, never wall clock: every wait is in GUEST seconds.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                               # noqa: E402
import os88ui                                       # noqa: E402
import os88build                                    # noqa: E402
import isotronlib as L                              # noqa: E402
import isotronsim as S                              # noqa: E402

MACHINE = "os8088_5150_cga_gla"
KEYS = ("h", "winner", "iter", "sp", "sc", "hnext", "lossrow", "rng", "ck", "nsteps")


def games(all_six):
    seen, out = set(), []
    for seed, tier, nr, hs in S.STORED:
        if all_six or tier not in seen:
            out.append((seed, tier, nr, hs))
            seen.add(tier)
    return out


def play_one(g, seed, tier, nr=None, hs=None, script=None):
    m = g.m
    model = S.Game(seed, tier)
    want = model.play(bot=script is None, script=script or ())
    if hs is not None:
        assert len(want) == nr and S.game_hash(want) == hs, \
            "the model no longer plays its stored game (seed %d tier %d) - run its selfcheck" % (seed, tier)
    # the negative control: the brief's simpler tower rule must play differently
    wrong = None
    if script is None:
        S.MUT["every_win"] = True
        try:
            wrong = S.Game(seed, tier).play()
        finally:
            S.MUT["every_win"] = False
        assert [r["h"] for r in wrong] != [r["h"] for r in want], \
            "seed %d: 'every win raises' plays the same towers - the row cannot tell the rules apart" % seed
    assert g.b("it_state") == L.ST_TITLE, "the game is not at its title (state %d)" % g.b("it_state")
    gover0 = g.w("it_gover")
    m.pause()
    g.put("it_tseed", seed, 2)
    g.put("it_tdiff", tier)
    g.put("it_tpause", 0)
    flags = L.TF_FAST | L.TF_NOFLOW | L.TF_HOLD
    if script is None:
        flags |= L.TF_BOT
        g.put("it_tinn", 0)
    else:
        assert len(script) <= L.TINMAX, \
            "a script of %d entries overruns it_tinput (IT_TINMAX = %d)" % (len(script), L.TINMAX)
        for k, (it, key) in enumerate(script):
            m.write(g.a("it_tinput") + 3 * k, bytes([it & 255, it >> 8, key]))
        g.put("it_tinn", len(script))
    g.put("it_tflags", flags)
    g.put("it_tgo", 1)
    m.run()
    t0 = m.status()["cycles"]
    got = []
    for k, rec in enumerate(want):
        M.until(m, lambda _: g.b("it_tpause") == 1,
                "round %d of seed %d to end" % (k + 1, seed), poll=0.2, guest=240)
        m.pause()
        st = g.state()
        m.run()
        got.append(st)
        bad = [(key, st[key], rec[key]) for key in KEYS if st[key] != rec[key]]
        if bad:
            raise AssertionError(
                "seed %d tier %d round %d: the guest and the model disagree on %s\n"
                "  guest: %r\n  model: %r" % (seed, tier, k + 1,
                                              ", ".join("%s (guest %r, model %r)" % b for b in bad),
                                              st, {x: rec[x] for x in KEYS}))
        print("    round %2d  h%d %s  it %5d  %2d-%-2d  next h%d  loss %d  rng %04x  ck %04x"
              % (k + 1, st["h"], st["winner"], st["iter"], st["sp"], st["sc"], st["hnext"],
                 st["lossrow"], st["rng"], st["ck"]), flush=True)
        g.put("it_tpause", 0)
    M.until(m, lambda _: g.w("it_gover") == gover0 + 1 and g.b("it_state") == L.ST_TITLE,
            "the game to end and return to the title", poll=0.2, guest=30)
    secs = (m.status()["cycles"] - t0) / M.GUEST_HZ
    assert len(got) == len(want)
    if script is None:
        assert S.has_tower_cycle(got), "the guest's game did not cover a tower 1-2-3-4-1"
        assert [r["h"] for r in got] != [r["h"] for r in wrong]
    assert got[-1]["lossrow"] == 3, "the guest's game did not end in three losses"
    print("  seed %d %s%s: %d rounds, %d head-steps, the guest equals the model after "
          "every round; tower %s; %.1f guest s" %
          (seed, S.TIERS[tier]["name"], " (scripted, no bot)" if script else "", len(got),
           sum(r["nsteps"] for r in got), "-".join(str(r["h"]) for r in got), secs), flush=True)
    m.pause()
    g.put("it_tinn", 0)
    m.run()
    return got


def keys_leg(g):
    """The keyboard's producer half and the between-rounds flow (docstring)."""
    m = g.m
    player = g.a("it_heads") + L.MAXHEADS * L.H_SIZE

    def ph(off):
        return m.read(player + off, 1)[0]

    def fifo():
        n = (g.b("it_fhead") - g.b("it_ftail")) & 255
        t = g.b("it_ftail")
        return [m.read(g.a("it_fifo") + ((t + k) & 3), 1)[0] for k in range(n)]

    def key(name):
        m.key(name)
        time.sleep(0.05)

    def wait(cond, what, guest=10):
        M.until(m, lambda _: cond(), what, poll=0.1, guest=guest)

    assert g.b("it_state") == L.ST_TITLE, "not at the title (state %d)" % g.b("it_state")
    m.pause()
    g.put("it_tflags", L.TF_HOLD)
    g.put("it_tpause", 1)                       # the worker's game is held
    m.run()
    key("Digit1")
    wait(lambda: g.b("it_state") == L.ST_PLAY, "1 at the title to start a game")
    assert g.b("it_tier") == 1 and g.w("it_round") == 0
    assert g.b("it_paused") == 0, \
        "the new game is paused at once - the isoTron window is not the front window"
    # P pauses; a turn while paused is refused; Space resumes
    key("KeyP")
    wait(lambda: g.b("it_paused") == 1, "P to pause")
    key("Delete")
    key("Space")
    wait(lambda: g.b("it_paused") == 0, "Space to resume a pause")
    assert fifo() == [], "a turn was posted while paused: %r" % fifo()
    # the FIFO: Del, Ins, Ins, Ins - three kept, the newest dropped
    for k in ("Delete", "Insert", "Insert", "Insert", "F10", "F10", "F9"):
        key(k)
    wait(lambda: g.b("it_kboost") == 2 and g.b("it_kf9") == 1, "F10 F10 F9 to be posted")
    got = fifo()
    assert got == [L.K_RIGHT, L.K_LEFT, L.K_LEFT], \
        "the FIFO after Del Ins Ins Ins holds %r, not right-left-left (depth 3, newest dropped)" % got
    assert g.w("it_iter") == 0, "the held game iterated"
    print("    keys: 1 starts, P pauses and refuses a turn, Space resumes; "
          "Del Ins Ins Ins -> FIFO %r; F10 F10 F9 -> boost 2, F9 1" % got, flush=True)
    # released: one iteration takes the speed, three cells take the turns
    m.pause()
    g.put("it_tpause", 0)
    m.run()
    wait(lambda: not fifo() and ph(L.H_HD) == L.HD_W, "the three turns to be taken")
    m.pause()
    sp, kb, k9, d, hd = g.b("it_pspeed"), g.b("it_kboost"), g.b("it_kf9"), ph(L.H_D), ph(L.H_HD)
    m.run()
    assert (kb, k9) == (0, 0), "the mailbox was not emptied (boost %d, F9 %d)" % (kb, k9)
    assert sp == L.P_NORMAL + 2, "speed %d/6 after F10 F10 F9, not %d" % (sp, L.P_NORMAL + 2)
    assert (d, hd) == (L.P_D0 + 1, L.HD_W), \
        "after right-left-left the player is at d %d heading %d, not d %d heading W" \
        % (d, hd, L.P_D0 + 1)
    print("    keys: speed %d/6; right-left-left is a U-turn: d %d heading W" % (sp, d), flush=True)
    # the round ends (HOLD freezes it in CRASH); a Space there is KEPT
    wait(lambda: g.b("it_tpause") == 1, "the round to end", guest=30)
    assert g.b("it_state") == L.ST_CRASH, "a round's end is not CRASH (state %d)" % g.b("it_state")
    key("Space")
    t = m.status()["cycles"]
    wait(lambda: m.status()["cycles"] - t > M.GUEST_HZ // 2, "half a guest second")
    assert g.b("it_qcont") == 1 and g.b("it_state") == L.ST_CRASH, \
        "Space during CRASH was lost (qcont %d, state %d)" % (g.b("it_qcont"), g.b("it_state"))
    m.pause()
    g.put("it_tpause", 0)
    m.run()
    wait(lambda: g.b("it_state") == L.ST_PLAY and g.w("it_round") == 1,
         "CRASH -> WAIT -> the next round on the Space pressed during CRASH")
    print("    keys: Space during CRASH kept; WAIT took it: round 2 in play", flush=True)
    key("Escape")
    wait(lambda: g.b("it_state") == L.ST_TITLE, "Esc to end the game")
    # GAME OVER -> the title: losses in a row at 2, so the next computer win ends it
    gover0 = g.w("it_gover")
    m.pause()
    g.put("it_tpause", 1)
    m.run()
    key("Digit1")
    wait(lambda: g.b("it_state") == L.ST_PLAY, "1 to start a second game")
    for _ in range(4):
        m.pause()
        g.put("it_lossrow", 2)
        g.put("it_tpause", 0)
        m.run()
        wait(lambda: g.b("it_tpause") == 1, "the round to end", guest=30)
        if g.b("it_state") == L.ST_OVER:
            break
        assert g.b("it_state") == L.ST_CRASH
        key("Space")                            # a player win: once more
    else:
        raise AssertionError("four rounds and the computer never won one")
    m.pause()
    g.put("it_tpause", 0)
    m.run()
    wait(lambda: g.b("it_state") == L.ST_TITLE and g.w("it_gover") == gover0 + 1,
         "GAME OVER to return to the title", guest=10)
    print("    keys: Esc ends a game; three losses: GAME OVER, then the title", flush=True)
    m.pause()
    g.put("it_tflags", 0)
    m.run()


def shot(m, out):
    os.makedirs(out, exist_ok=True)
    m.pause()
    w, h, rows = m.vram("cga")
    m.run()
    p = os.path.join(out, "isotron-w1-cga.png")
    M.write_png(p, w, h, rows)
    print("  kept %s" % p)


def rules(args):
    t0 = time.time()
    with os88ui.boot(os88build.at("build/os8088-360.img"),
                     apps=os88build.at("build/isotron360.img"), machine=MACHINE) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        for seed, tier, nr, hs in games(args.all):
            print("  %s, seed %d (%d rounds stored)" % (S.TIERS[tier]["name"], seed, nr), flush=True)
            play_one(g, seed, tier, nr, hs)
            if args.shot:
                shot(m, args.shot)
                args.shot = None
        # THE SCRIPTED PATH (it_tinput, no bot): two F10s, a right turn, a
        # left and an F9 at fixed iterations of every round - the keys a
        # player would post, through the same FIFO and boost/F9 bytes
        seed, tier = S.SCRIPT_GAME
        print("  %s, seed %d, scripted" % (S.TIERS[tier]["name"], seed), flush=True)
        play_one(g, seed, tier, script=S.SCRIPT)
        # the harness off again: the title is a title
        m.pause()
        g.put("it_tflags", 0)
        m.run()
        print("  the keyboard, and the flow between rounds", flush=True)
        keys_leg(g)
    print("isotronrules: ok (%.0f s wall)" % (time.time() - t0))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rules", action="store_true", help="the isotronrules row")
    ap.add_argument("--all", action="store_true", help="all six stored games")
    ap.add_argument("--shot", metavar="DIR", help="keep a capture of the window")
    a = ap.parse_args()
    if a.rules:
        return rules(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
