#!/usr/bin/env python3
"""isoTron on the real 8088 against the host model (SPEC.md 108.3 - 108.11).

    make isotrondisk && python3 tests/isotron.py --rules [--all] [--shot DIR]
    python3 tests/isotron.py --bound
    python3 tests/isotron.py --win cga|herc|vga|ega|all
    python3 tests/isotron.py --perf cga|herc|vga|all [--json FILE]

BOUND (isotronbound, wave 2): two stored games on the 5150 CGA; after every
round the guest's 64,000-byte canvas byte for byte the model's, and the bound
counters - the most canvas bytes a step's k* scan examined and the most rows
it wrote, both COUNTED in it_span and both equal to the model's - with the
model's "probe limit 21" required to disagree with what the guest counted.

WIN (isotronwin; isotronega with `--win ega`, a private VIDEO=ega kernel on
the XT-VGA): the title and four rounds of a seeded game, the window's
picture read off the card and compared pixel for pixel with the model's
windowed presenter of the same canvas, outside the overlay text rects.
ISOTRON_SHOTS=DIR keeps every capture.

PERF (isotronperf): MartyPC's cycle counter - the computer head-step whole
(the median over EVERY computer head-step of two whole rounds of a seeded FAST
game, the count held to the model's, so the sample is the same head-steps on
every run) and the paced wake at Rookie h1 and
Lunatic h4 at full boost, against ISOTRON-PLAN section 16's ceilings, and the
presentation governor engaged after any wake over 45 ms. Then A ROUND'S START
at h1 and h4: the rebuild, the hold of the wake that makes it, every hold
after it until the whole present is out in bands (each under 50 ms), and the
span from the rebuild to the last band - reported, the ceiling being wave
3's toggle and wave 4's sweep (SPEC.md 108.11.7).

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
human to look at; nothing is asserted on it - `--win` is where the picture is
held to the model).

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
    p = os.path.join(out, "isotron-cga.png")
    M.write_png(p, w, h, rows)
    print("  kept %s" % p)


def rules(args):
    t0 = time.time()
    with os88ui.boot(os88build.at("build/os8088-360.img"),
                     apps=L.fresh_disk(os88build.at("build/isotron360.img")),
                     machine=MACHINE) as ui:
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


# =============================================================================
# WAVE 2: THE PICTURE (SPEC.md 108.9 - 108.11)
# =============================================================================
MACHINES = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla",
            "vga": "os8088_xt_vga", "ega": "os8088_xt_vga"}
REPORTS = os.environ.get("ISOTRON_SHOTS")   # keep the captures here, if set


def start_game(g, seed, tier, flags):
    """A harness game from the title: seed, tier, flags; returns at once."""
    m = g.m
    assert g.b("it_state") == L.ST_TITLE, "not at the title (state %d)" % g.b("it_state")
    m.pause()
    g.put("it_tseed", seed, 2)
    g.put("it_tdiff", tier)
    g.put("it_tpause", 0)
    g.put("it_tinn", 0)
    g.put("it_tflags", flags)
    g.put("it_tgo", 1)
    m.run()


def to_title(g):
    m = g.m
    m.pause()
    g.put("it_tflags", 0)
    g.put("it_tpause", 0)
    g.put("it_qesc", 1)
    m.run()
    M.until(m, lambda _: g.b("it_state") == L.ST_TITLE, "the title", poll=0.1, guest=10)


def presented(ui, g):
    """Park the pointer off the window and wait until nothing is owed: every
    box, overlay and whole present the guest had has reached the glass."""
    ui.mo.to(2, 2)
    M.until(g.m, lambda _: not g.owed(), "the picture to be presented", poll=0.1, guest=30)
    M.guest_sleep(g.m, 0.3)


def nearest(palette):
    cache = {}

    def f(rgb):
        if rgb not in cache:
            cache[rgb] = min(range(len(palette)), key=lambda i: sum(
                (a - b) ** 2 for a, b in zip(palette[i], rgb)))
        return cache[rgb]
    return f


def glass(m, tag, name=None):
    """The screen as rows of pixels: 0/1 on a 1bpp card (its framebuffer),
    EGA indices on a VGA (the card's own rendering, mode 12h or 10h)."""
    m.pause()
    try:
        if tag in ("cga", "herc"):
            w, h, rows = m.vram(tag)
            if REPORTS and name:
                M.write_png(os.path.join(REPORTS, name + ".png"), w, h, rows)
            return w, h, [bytes(r) for r in rows]
        w, h, data = m.fbuf(0)
    finally:
        m.run()
    if REPORTS and name:
        M.write_png_rgb(os.path.join(REPORTS, name + ".png"), w, h, data)
    f = nearest(S.EGA_RGB)
    rows = []
    for y in range(h):
        o = y * w * 3
        rows.append(bytes(f(tuple(data[o + 3 * x:o + 3 * x + 3])) for x in range(w)))
    return w, h, rows


def check_glass(g, tag, canvas, what, name=None):
    """The window's picture on the glass against the model's presenter of the
    same canvas (win1 on a 1bpp display, win4 on a colour one), pixel for
    pixel, outside the overlay rects; the content around the picture black;
    and the overlays' text actually there. Returns the pixels compared."""
    m = g.m
    geo = g.geom()
    assert geo["layok"], "the picture does not fit (%r)" % geo
    mono = tag in ("cga", "herc")
    assert geo["surf"] == (0 if mono else 1), \
        "%s: the surface is %s, not %s" % (tag, geo["surf"], "mono" if mono else "colour")
    hc, ox, oy, pwb = geo["hc"], geo["ox"], geo["oy"], geo["pwb"]
    assert ox % 8 == 0, "the picture's origin %d is off the byte grid" % ox
    assert hc == min(geo["ch"], S.WIN_HMAX), "Hc %d for a content %d high" % (hc, geo["ch"])
    ovs = g.overlays()
    w, h, rows = glass(m, tag, name)
    want = S.win1(canvas, hc) if mono else S.win4(canvas, hc)
    bad, first, n = 0, None, 0
    for r in range(hc):
        row, wr = rows[oy + r], want[r]
        cut = [(x0, x1) for x0, y0, x1, y1, _ in ovs if y0 <= r < y1]
        for X in range(pwb * 8):
            if any(x0 <= X < x1 for x0, x1 in cut):
                continue
            n += 1
            if row[ox + X] != wr[X]:
                bad += 1
                if first is None:
                    first = (X, r, row[ox + X], wr[X])
    assert not bad, ("%s %s: %d of %d pixels differ from the model's presenter; first at "
                     "picture (%d, %d): glass %r, model %r" % ((tag, what, bad, n) + first))
    # around the picture: the content's other pixels are black, once filled
    blk = 0
    for y in range(geo["cy"], geo["cy"] + geo["ch"]):
        for x in range(geo["cx"], geo["cx"] + geo["cw"]):
            inside = ox <= x < ox + pwb * 8 and oy <= y < oy + hc
            if not inside and rows[y][x] != 0:
                blk += 1
    assert not blk, "%s %s: %d pixels of the content around the picture are not black" \
        % (tag, what, blk)
    for x0, y0, x1, y1, txt in ovs:
        lit = sum(1 for y in range(oy + y0, oy + y1) for x in range(ox + x0, ox + x1)
                  if rows[y][x])
        assert lit > 4 * len(txt), "%s %s: the overlay %r is not on the glass" % (tag, what, txt)
    return n, [t for _, _, _, _, t in ovs]


def boot_for(tag):
    """(system disk, apps disk, machine) for an adapter's row."""
    system = os88build.at("build/os8088-360.img")
    if tag == "ega":
        tree = os88build.tree("VIDEO=ega").apply()
        system = tree.img("os8088-360.img")
    return system, L.fresh_disk(os88build.at("build/isotron360.img")), MACHINES[tag]


def win_leg(tag, rounds=4):
    """isotronwin / isotronega: the title and a seeded game's rounds, each
    on the glass equal to the model's windowed presenter of the model's own
    canvas - which is the guest's canvas too, held byte for byte."""
    system, apps, machine = boot_for(tag)
    seed, tier = S.STORED[0][0], S.STORED[0][1]
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        geo = g.geom()
        print("  %s: content %dx%d at (%d,%d); picture at (%d,%d), %d bytes x %d rows, %s"
              % (tag, geo["cw"], geo["ch"], geo["cx"], geo["cy"], geo["ox"], geo["oy"],
                 geo["pwb"], geo["hc"], "colour" if geo["surf"] else "mono"), flush=True)
        # --- the title -----------------------------------------------------------
        tg, tc = S.Grid(), S.Canvas()
        tc.title(tg)
        assert g.canvas() == tc.pack(), "the title's canvas differs from the model's"
        n, ovs = check_glass(g, tag, tc, "title", "w2-%s-title" % tag)
        print("    title: %d pixels equal the model's; overlays %r" % (n, ovs), flush=True)
        # --- a game, round by round ----------------------------------------------
        model = S.Game(seed, tier, canvas=True)
        start_game(g, seed, tier, L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        for k in range(rounds):
            rec = model.play_round()
            M.until(m, lambda _: g.b("it_tpause") == 1, "round %d to end" % (k + 1),
                    poll=0.2, guest=240)
            presented(ui, g)
            st = g.state()
            assert (st["h"], st["ck"], st["iter"]) == (rec["h"], rec["ck"], rec["iter"]), \
                "round %d: the guest played a different round (%r)" % (k + 1, st)
            assert g.canvas() == model.canvas.pack(), "round %d: the canvas differs" % (k + 1)
            n, ovs = check_glass(g, tag, model.canvas, "round %d" % (k + 1),
                                 "w2-%s-r%d" % (tag, k + 1))
            print("    round %d (h%d, %s): %d pixels equal the model's; overlays %r"
                  % (k + 1, rec["h"], rec["winner"], n, ovs), flush=True)
            m.pause()
            g.put("it_tpause", 0)
            m.run()
        to_title(g)
    return 0


def bound_leg():
    """isotronbound: the step's bound counters and the whole canvas, after
    every round of two stored games, against the model."""
    system, apps, machine = boot_for("cga")
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        tg, tc = S.Grid(), S.Canvas()
        tc.title(tg)
        assert g.canvas() == tc.pack(), "the title's 64,000 bytes differ from the model's"
        print("  the title: 64,000 bytes equal the model's", flush=True)
        seen = []                       # (seed, round, guest's most probes)
        for seed, tier, nr, hs in (S.STORED[0], S.STORED[4]):
            model = S.Game(seed, tier, canvas=True)
            start_game(g, seed, tier, L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
            k = 0
            while not model.over():
                rec = model.play_round()
                k += 1
                M.until(m, lambda _: g.b("it_tpause") == 1, "round %d to end" % k,
                        poll=0.2, guest=240)
                # the round's last boxes reach the glass in a wake or two
                M.until(m, lambda _: not g.owed(), "the round's boxes", poll=0.1, guest=30)
                m.pause()
                st = g.state()
                mp, ms = g.b("it_maxprobe"), g.b("it_maxspan")
                raw = g.canvas()
                g.put("it_tpause", 0)
                m.run()
                assert (st["ck"], st["nsteps"]) == (rec["ck"], rec["nsteps"]), \
                    "seed %d round %d: a different round (%r)" % (seed, k, st)
                assert mp <= S.PROBES and ms <= S.WALL_H, \
                    "seed %d round %d: %d probes, %d rows" % (seed, k, mp, ms)
                seen.append((seed, k, mp))
                assert (mp, ms) == (rec["maxprobe"], rec["maxspan"]), \
                    "seed %d round %d: most probes/rows %d/%d, the model's %d/%d" \
                    % (seed, k, mp, ms, rec["maxprobe"], rec["maxspan"])
                if raw != model.canvas.pack():
                    want = model.canvas.pack()
                    i = next(j for j in range(len(raw)) if raw[j] != want[j])
                    raise AssertionError("seed %d round %d: the canvas differs from the "
                                         "model's at byte %d (column %d, row %d)"
                                         % (seed, k, i, i // S.CCOL, i % S.CCOL))
                print("    seed %d round %d: h%d, %d steps, most probes %d, most rows %d; "
                      "64,000 canvas bytes equal the model's"
                      % (seed, k, rec["h"], rec["nsteps"], mp, ms), flush=True)
            M.until(m, lambda _: g.b("it_state") == L.ST_TITLE, "the title", poll=0.2, guest=60)
    # THE NEGATIVE CONTROL: a scan that stops at 21 bytes must count
    # differently from what the guest counted, or the probe row could not
    # tell a broken bound from the real one
    S.MUT["probe_limit"] = 21
    try:
        mut = []
        for seed, tier, nr, hs in (S.STORED[0], S.STORED[4]):
            mg = S.Game(seed, tier, canvas=True)
            k = 0
            while not mg.over() and k < sum(1 for s_, _, _ in seen if s_ == seed):
                k += 1
                mut.append((seed, k, mg.play_round()["maxprobe"]))
    finally:
        S.MUT["probe_limit"] = S.PROBES
    assert mut != seen, "a probe limit of 21 counts what the guest counted - the row " \
        "cannot tell the bound"
    print("  negative control: a probe limit of 21 counts differently (%d of %d rounds)"
          % (sum(1 for a, b in zip(mut, seen) if a != b), len(seen)), flush=True)
    scan21_leg()
    print("isotronbound: ok")
    return 0


def scan21_leg():
    """THE GUEST'S OWN NEGATIVE CONTROL: the package assembled with -DIT_SCAN21
    (its k* scan one row short, IT_PROBES = 21), on a scratch floppy of its
    own, playing the first stored game against the TRUE model, must differ -
    in its probe count or in its canvas - within the game's first rounds.
    The checks above would pass a guest whose scan could not reach the 22nd
    row only if the model's limit moved with it; this is the guest moving
    alone, and the same comparison refusing it."""
    import subprocess
    import tempfile
    d = os88build.at("build/isoscan21")
    os.makedirs(d, exist_ok=True)
    o88 = os.path.join(d, "isotron.o88")
    with tempfile.TemporaryDirectory() as td:
        binf = os.path.join(td, "isotron21.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-DIT_SCAN21",
                        "-I", os.path.join(ROOT, "apps") + os.sep,
                        "-I", os.path.join(ROOT, "apps", "isotron") + os.sep,
                        "-o", binf, os.path.join(ROOT, "apps", "isotron", "isotron.asm")],
                       check=True, cwd=ROOT)
        ship = os.path.join(td, "ship.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", os.path.join(ROOT, "apps") + os.sep,
                        "-I", os.path.join(ROOT, "apps", "isotron") + os.sep,
                        "-o", ship, os.path.join(ROOT, "apps", "isotron", "isotron.asm")],
                       check=True, cwd=ROOT)
        a, b = open(binf, "rb").read(), open(ship, "rb").read()
        # THE SAME LAYOUT: only the scan's two immediates move, so every
        # offset tests/isotronlib.py reads out of the shipped source holds
        assert len(a) == len(b), "the IT_SCAN21 image is a different size"
        diff = [i for i in range(len(a)) if a[i] != b[i]]
        assert len(diff) == 2 and all(a[i] == 21 and b[i] == 22 for i in diff), \
            "the IT_SCAN21 image differs in more than the scan's limit: %r" % diff[:8]
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
                        "--compress-if=lz4", binf, "-o", o88], check=True, cwd=ROOT)
    apps = M.scratch_disk(os88build.at("build/isoscan21.img"), o88)
    system = os88build.at("build/os8088-360.img")
    with os88ui.boot(system, apps=apps, machine=MACHINES["cga"]) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        seed, tier, nr, hs = S.STORED[0]
        model = S.Game(seed, tier, canvas=True)
        start_game(g, seed, tier, L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        k, caught = 0, None
        while not model.over() and k < 4 and caught is None:
            rec = model.play_round()
            k += 1
            M.until(m, lambda _: g.b("it_tpause") == 1, "round %d to end" % k,
                    poll=0.2, guest=240)
            m.pause()
            mp, ms = g.b("it_maxprobe"), g.b("it_maxspan")
            raw = g.canvas()
            g.put("it_tpause", 0)
            m.run()
            if (mp, ms) != (rec["maxprobe"], rec["maxspan"]):
                caught = "round %d: most probes/rows %d/%d, the model's %d/%d" \
                    % (k, mp, ms, rec["maxprobe"], rec["maxspan"])
            elif raw != model.canvas.pack():
                want = model.canvas.pack()
                caught = "round %d: the canvas differs in %d bytes" \
                    % (k, sum(1 for x, y in zip(raw, want) if x != y))
        to_title(g)
    assert caught, "a guest whose scan stops at 21 rows played %d rounds equal to the " \
        "model - the row cannot tell the bound" % k
    print("  negative control, the guest: built with IT_PROBES = 21 it is refused at %s"
          % caught, flush=True)


# --- isotronperf, the windowed part (SPEC.md 108.11.7) -------------------------
# THE HEAD-STEP'S SAMPLE IS WHOLE ROUNDS of a seeded game (SPEC.md 108.10.3):
# every computer head-step from the round's first to the HOLD at its end, so
# it is the same head-steps on every run and every adapter. It was six
# windows of ~50 head-steps 2.5 guest seconds apart, and WHERE those landed in
# the game moved with the harness's host-timed pokes: the Lunatic median read
# 2,581 - 3,072 on one game, over the ceiling one run in nine. Every round of
# that game traced whole reads 2,648 - 2,691.
PERF_ROUNDS = ((2425, 1, "rookie", (1, 2)), (702, 3, "lunatic", (2, 3)))
CEIL = {  # ISOTRON-PLAN section 16: cycles, then ms
    "step": 3000,
    "band_hold": 50.0,                  # a hold while a whole present goes out
    "wake_rookie": {"cga": 22.0, "herc": 22.0, "vga": 30.0},
    "wake_lunatic4": {"cga": 35.0, "herc": 35.0, "vga": 50.0},
}


def comp_steps(seed, tier):
    """The model's computer head-steps per round of a seeded bot game: every
    `it_hs0 .. it_hs1` pair the guest's round must hold, crashes included."""
    per = {}
    orig = S.Round._computer

    def counted(rnd):
        per[id(rnd)] = per.get(id(rnd), 0) + sum(1 for c in rnd.heads if c.alive)
        return orig(rnd)
    rounds_seen = []
    orig_init = S.Round.__init__

    def init(rnd, *a, **kw):
        orig_init(rnd, *a, **kw)
        rounds_seen.append(rnd)
    S.Round._computer, S.Round.__init__ = counted, init
    try:
        S.Game(seed, tier).play()
    finally:
        S.Round._computer, S.Round.__init__ = orig, orig_init
    return [per.get(id(r), 0) for r in rounds_seen]


def pairs(hits, a, b):
    out, last = [], None
    for h in hits:
        if h["addr"] == a:
            last = h
        elif h["addr"] == b and last is not None:
            out.append((h["cycles"] - last["cycles"], last))
            last = None
    return out


def stats(xs):
    xs = sorted(xs)
    return dict(n=len(xs), min=xs[0], med=xs[len(xs) // 2], p90=xs[int(len(xs) * 0.9)],
                max=xs[-1])


def perf_leg(tag, out):
    system, apps, machine = boot_for(tag)
    res = {}
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        B = g.base
        HS0, HS1 = B + g.code["it_hs0"], B + g.code["it_hs1"]
        AI1 = B + g.code["it_ai1"]
        W0, W1 = B + g.code["it_wake0"], B + g.code["it_wake1"]
        # --- the head-step: logic + canvas, FAST games at Rookie and Lunatic ------
        # WHOLE ROUNDS (PERF_ROUNDS): every computer head-step of each, from
        # the round's open field to its crowded end, the count held to the
        # model's - 1,049 at Rookie (h1, h1) and 3,177 at Lunatic (h1, h2)
        for seed, tier, lab, rounds in PERF_ROUNDS:
            hs_all, mv_all = [], []
            want = comp_steps(seed, tier)
            for r in range(1, max(rounds) + 1):
                if r == 1:
                    # the game's first round starts on it_tgo: everything
                    # else poked first, so the trace arms before ITS first step
                    assert g.b("it_state") == L.ST_TITLE
                    m.pause()
                    g.put("it_tseed", seed, 2)
                    g.put("it_tdiff", tier)
                    g.put("it_tpause", 0)
                    g.put("it_tinn", 0)
                    g.put("it_tflags", L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
                    m.run()
                    go = ("it_tgo", 1)
                else:
                    go = ("it_tpause", 0)   # held at round r - 1's end
                if r not in rounds:
                    g.put(*go)
                    M.until(m, lambda _: g.b("it_tpause") == 1, "round %d's end" % r,
                            poll=0.1, guest=120)
                    continue
                # EVERY computer head-step of the round, from its first to the
                # hold at its end: one write starts it (no pause inside the
                # trace) and the game is a pure function of its seed, so the
                # sample is the same head-steps on every run and adapter
                with M.bp_trace(m, HS0, AI1, HS1, cap=12000) as tr:
                    g.put(*go)
                    tr.until(lambda: g.b("it_tpause") == 1, "round %d's end" % r,
                             limit=300)
                assert not tr.overflowed, "%s round %d: the trace overflowed" % (lab, r)
                hs_r = [c for c, _ in pairs(tr.hits, HS0, HS1)]
                assert len(hs_r) == want[r - 1], "%s round %d: %d head-steps traced, the " \
                    "model's round has %d" % (lab, r, len(hs_r), want[r - 1])
                hs_all += hs_r
                mv_all += [c for c, _ in pairs(tr.hits, AI1, HS1)]
                print("  %s round %d (h%d): %d head-steps, median %d"
                      % (lab, r, g.b("it_hplayed"), len(hs_r), stats(hs_r)["med"]), flush=True)
            hs, mv = stats(hs_all), stats(mv_all)
            res["headstep_" + lab], res["movespan_" + lab] = hs, mv
            print("  %s head-step (rand+AI+move+canvas) %s; move+canvas %s"
                  % (lab, hs, mv), flush=True)
            to_title(g)
            presented(ui, g)

        # --- the paced wake --------------------------------------------------------
        def wakes(lab, n=120):
            st_addr = g.a("it_state")
            gs_addr = g.a("it_gskip")
            fu_addr = g.a("it_full")

            def onhit(mm, rec):
                return (mm.read(st_addr, 1)[0], mm.read(gs_addr, 1)[0],
                        mm.read(fu_addr, 1)[0])
            with M.bp_trace(m, W0, W1, cap=2 * n + 10, on_hit=onhit) as tr:
                tr.wait(2 * n, limit=600)
            # A WAKE IN PLAY, both ends: one whose round ended inside it
            # letters the result (a VGA's font_run is ~1 ms a cell), and the
            # wakes after a round's start make its whole present in bands
            # (it_full at the wake's start) - neither is the steady state the
            # ceilings are about, and the round-start lines below price them
            ws = []
            last = None
            over, engaged = False, False    # a wake over 45 ms, and the
            for h in tr.hits:               # governor on AFTER it (108.11.6)
                if h["addr"] == W0:
                    last = h
                elif h["addr"] == W1 and last is not None:
                    if last["hit"][0] == L.ST_PLAY and h["hit"][0] == L.ST_PLAY \
                            and not last["hit"][2]:
                        c = h["cycles"] - last["cycles"]
                        ws.append(c)
                        if 1000.0 * c / M.GUEST_HZ > 45.0:
                            over = True
                    if over and h["hit"][1]:
                        engaged = True
                    if h["hit"][1]:
                        res["governor_" + lab + "_seen"] = 1
                    last = None
            s = stats(ws)
            s["ms_med"] = 1000.0 * s["med"] / M.GUEST_HZ
            s["ms_max"] = 1000.0 * s["max"] / M.GUEST_HZ
            s["over45"], s["engaged"] = over, engaged
            s["governor_seen"] = res.get("governor_" + lab + "_seen", 0)
            res["wake_" + lab] = s
            print("  %s wake %s = %.1f ms median, %.1f ms most; governor engaged in the "
                  "trace: %s" % (lab, s, s["ms_med"], s["ms_max"],
                                 "yes" if s["governor_seen"] else "no"), flush=True)
            return s
        # (play starts when the round's whole present is out: it_vsync)
        live = lambda _: g.b("it_state") == L.ST_PLAY and not g.b("it_vsync")  # noqa: E731
        start_game(g, 2425, 1, L.TF_BOT | L.TF_NOFLOW)
        M.until(m, live, "play", poll=0.1, guest=20)
        M.guest_sleep(m, 1.0)
        rook = wakes("rookie")
        to_title(g)
        # Lunatic at h = 4, full boost: round 1 held, the tower poked to 4, the
        # speed to 9 once that round is in play
        start_game(g, 702, 3, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        M.until(m, lambda _: g.b("it_tpause") == 1, "round 1", poll=0.2, guest=120)
        m.pause()
        g.put("it_h", 4)
        g.put("it_tflags", L.TF_BOT | L.TF_NOFLOW)
        g.put("it_tpause", 0)
        m.run()
        M.until(m, lambda _: live(m) and g.b("it_hplayed") == 4,
                "a round at h4", poll=0.1, guest=30)
        M.guest_sleep(m, 0.5)
        m.pause()
        g.put("it_pspeed", 9)
        m.run()
        luna = wakes("lunatic4")
        to_title(g)

        # --- A ROUND'S START (SPEC.md 108.11.7): the rebuild, its wake's hold,
        # the holds that put the whole picture out in bands, and the span -
        # at h1 (a game's first round) and h4 (the next one, the tower poked)
        RB0, RB1, FD = B + g.code["it_rb0"], B + g.code["it_rb1"], B + g.code["it_fdone"]

        def rb_done(hs):
            """The index of the first it_fdone AFTER an it_rb1, or None: the
            round's own whole present out. ANY it_fdone will not do - a
            present already going out when the trace armed (the title's, or
            a hold's message) ends with one BEFORE the rebuild, and a wait
            satisfied by that one read a trace with no it_rb0 in it."""
            i1 = next((i for i, h in enumerate(hs) if h["addr"] == RB1), None)
            if i1 is None:
                return None
            return next((i for i in range(i1 + 1, len(hs)) if hs[i]["addr"] == FD), None)

        def roundstart(lab, prep, trigger):
            """`prep` sets up everything but the ONE byte that starts the
            round, before the trace arms - it may pause and poke. `trigger`
            writes that byte INSIDE the trace and must not pause: a pause
            from this thread can land on a stop the pump has not recorded
            yet, and the `run` after it then resumes past that stop - a lost
            it_wake0 charged the rebuild to the wake before it ("431 ms in a
            1 ms hold") and a lost it_rb0 was a StopIteration."""
            prep()
            with M.bp_trace(m, W0, W1, RB0, RB1, FD, cap=8000) as tr:
                trigger()
                tr.until(lambda: rb_done(tr.hits) is not None,
                         "the %s round's whole present" % lab, limit=60)
            assert not tr.overflowed, "%s round start: the trace overflowed" % lab
            hs = tr.hits
            # THE STOPS MUST BE WHOLE: wakes alternate W0, W1, and the rebuild
            # sits inside one. A trace that breaks either is not attributed -
            # it is a harness fault, and naming it beats a wrong number.
            ws = [h["addr"] for h in hs if h["addr"] in (W0, W1)]
            for a, b in zip(ws, ws[1:]):
                assert a != b, "%s round start: a lost wake mark in the trace" % lab
            i0 = next(i for i, h in enumerate(hs) if h["addr"] == RB0)
            i1 = next(i for i, h in enumerate(hs) if h["addr"] == RB1 and i > i0)
            fd = rb_done(hs)
            # the wake that rebuilt: from its W0, the last mark of either kind
            # before the rebuild - or, when the trace was armed inside that
            # very wake, from the rebuild itself, a lower bound
            wk = [i for i in range(i0) if hs[i]["addr"] in (W0, W1)]
            if wk and hs[wk[-1]]["addr"] == W0:
                w0, cur = wk[-1], None
            else:
                assert not wk or hs[wk[-1]]["addr"] == W1
                w0, cur = i0, hs[i0]["cycles"]
            holds = []
            for i in range(w0, len(hs)):
                if hs[i]["addr"] == W0:
                    cur = hs[i]["cycles"]
                elif hs[i]["addr"] == W1 and cur is not None:
                    holds.append(hs[i]["cycles"] - cur)
                    cur = None
                    if i > fd:
                        break
            ms = lambda c: 1000.0 * c / M.GUEST_HZ       # noqa: E731
            r = dict(rebuild=hs[i1]["cycles"] - hs[i0]["cycles"], hold=holds[0],
                     bands=len(holds) - 1, band_max=max(holds[1:] or [0]),
                     span=hs[fd]["cycles"] - hs[i1]["cycles"])
            assert r["hold"] >= r["rebuild"], "%s round start: a %d-cycle hold cannot " \
                "hold a %d-cycle rebuild" % (lab, r["hold"], r["rebuild"])
            r.update({k + "_ms": ms(r[k]) for k in ("rebuild", "hold", "band_max", "span")})
            res["roundstart_" + lab] = r
            print("  %s round start: rebuild %d cycles (%.0f ms) in a %.0f ms hold; then %d "
                  "band holds, the longest %.1f ms; whole present out %.0f ms after the "
                  "rebuild" % (lab, r["rebuild"], r["rebuild_ms"], r["hold_ms"], r["bands"],
                               r["band_max_ms"], r["span_ms"]), flush=True)
            return r

        def h1_prep():
            presented(ui, g)            # the title's own present all out first
            m.pause()
            g.put("it_tseed", 2425, 2)
            g.put("it_tdiff", 1)
            g.put("it_tpause", 0)
            g.put("it_tinn", 0)
            g.put("it_tflags", L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
            m.run()
        assert g.b("it_state") == L.ST_TITLE
        rs1 = roundstart("h1", h1_prep, lambda: g.put("it_tgo", 1))
        m.pause()                       # the rest of round 1 FAST: only its start
        g.put("it_tflags", L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        m.run()
        M.until(m, lambda _: g.b("it_tpause") == 1, "round 1's end", poll=0.2, guest=120)

        def h4_prep():
            m.pause()                   # held (it_tpause = 1): nothing moves
            g.put("it_h", 4)
            g.put("it_tflags", L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
            m.run()
        rs4 = roundstart("h4", h4_prep, lambda: g.put("it_tpause", 0))
        to_title(g)
    kind = "vga" if tag in ("vga", "ega") else tag
    bad = []
    for lab in ("rookie", "lunatic"):
        if res["headstep_" + lab]["med"] > CEIL["step"]:
            bad.append("%s head-step %d cycles > %d" % (lab, res["headstep_" + lab]["med"],
                                                       CEIL["step"]))
    if rook["ms_med"] > CEIL["wake_rookie"][kind]:
        bad.append("Rookie h1 wake %.1f ms > %.1f" % (rook["ms_med"], CEIL["wake_rookie"][kind]))
    if luna["ms_med"] > CEIL["wake_lunatic4"][kind]:
        bad.append("Lunatic h4 wake %.1f ms > %.1f" % (luna["ms_med"], CEIL["wake_lunatic4"][kind]))
    for lab, w in (("Rookie h1", rook), ("Lunatic h4", luna)):
        if w["over45"] and not w["engaged"]:
            bad.append("%s: a wake over 45 ms and the governor never engaged after it" % lab)
    for lab, r in (("h1", rs1), ("h4", rs4)):
        if r["band_max_ms"] > CEIL["band_hold"]:
            bad.append("%s round start: a band's hold %.1f ms > %.1f"
                       % (lab, r["band_max_ms"], CEIL["band_hold"]))
    if out:
        import json
        json.dump(res, open(out, "w"), indent=1)
    assert not bad, "%s: %s" % (tag, "; ".join(bad))
    print("  %s: under every section 16 ceiling" % tag, flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rules", action="store_true", help="the isotronrules row")
    ap.add_argument("--all", action="store_true", help="all six stored games")
    ap.add_argument("--shot", metavar="DIR", help="keep a capture of the window")
    ap.add_argument("--bound", action="store_true", help="the isotronbound row")
    ap.add_argument("--win", metavar="TAG", help="cga, herc, vga, ega or all: isotronwin(ega)")
    ap.add_argument("--perf", metavar="TAG", help="cga, herc, vga or all: isotronperf")
    ap.add_argument("--json", metavar="FILE", help="--perf: keep the numbers")
    a = ap.parse_args()
    if a.rules:
        return rules(a)
    if a.bound:
        return bound_leg()
    if a.win:
        for tag in (("cga", "herc", "vga") if a.win == "all" else a.win.split(",")):
            win_leg(tag)
        print("isotronwin: ok (%s)" % a.win)
        return 0
    if a.perf:
        for tag in (("cga", "herc", "vga") if a.perf == "all" else a.perf.split(",")):
            perf_leg(tag, a.json and a.json.replace(".json", "-%s.json" % tag))
        print("isotronperf: ok (%s)" % a.perf)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
