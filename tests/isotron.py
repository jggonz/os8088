#!/usr/bin/env python3
"""isoTron on the real 8088 against the host model (SPEC.md 108.3 - 108.11).

    make isotrondisk && python3 tests/isotron.py --rules [--all] [--shot DIR]
    python3 tests/isotron.py --bound
    python3 tests/isotron.py --win cga|herc|vga|ega|all
    python3 tests/isotron.py --perf cga|herc|vga|all [--fsonly] [--json FILE]
    python3 tests/isotron.py --fs cga|herc|vga|ega|all
    python3 tests/isotron.py --toggle cga|vga|all
    python3 tests/isotron.py --twodisp

FULL SCREEN (wave 3, SPEC.md 108.12): --fs is isotronfs (and `--win ega`
runs the EGA's full-screen half after its windowed one), --toggle is
isotrontoggle, --twodisp isotrontwodisp, and --perf adds a full-screen boot
a machine (--fsonly: that alone) - see each leg's docstring.

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
import struct
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
    """End the game; return once the TITLE'S whole present has gone out.
    it_to_title sets ST_TITLE BEFORE it builds the title's canvas and owes
    its present, so a wait on the state alone can read the glass in the
    build's window, with nothing owed yet - the present counter is the
    title's own (it_fsn in a bracket, it_nwhole windowed)."""
    m = g.m
    key = "it_fsn" if g.b("it_fsm") else "it_nwhole"
    n0 = g.w(key)
    m.pause()
    g.put("it_tflags", 0)
    g.put("it_tpause", 0)
    g.put("it_qesc", 1)
    m.run()
    M.until(m, lambda _: g.b("it_state") == L.ST_TITLE and g.w(key) != n0, "the title",
            poll=0.1, guest=20)


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
        w, h, data = vga_desk(m)
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


def vga_desk(m):
    """The VGA desktop's rendering (mode 12h, or 10h on an EGA kernel). AFTER
    A 320 x 200 BRACKET MartyPC's usual aperture keeps answering that mode's
    400 lines (tests/pxfsx.py found it on 13h; a 0Dh bracket does the same),
    so then the desktop is cut out of the WHOLE FIELD (aperture 3), where
    MartyPC places a 12h frame at (96, 32). The machine must be paused."""
    w, h, data = m.fbuf(0)
    if (w, h) in ((640, 480), (640, 350)):
        return w, h, data
    fw, fh, rgb = m.fbuf(3)
    ox, oy, W, H = 96, 32, 640, 480
    out = bytearray()
    for y in range(H):
        o = 3 * ((oy + y) * fw + ox)
        out += rgb[o:o + 3 * W]
    return W, H, bytes(out)


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
        prev = None
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


# =============================================================================
# WAVE 3: FULL SCREEN (SPEC.md 108.12)
# =============================================================================
FS_MACHINES = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla",
               "vga": "os8088_xt_vga", "ega": "os8088_xt_vga"}


def fs_settled(g, what="the full-screen picture"):
    """In the bracket, with nothing owed: a frame has presented everything."""
    M.until(g.m, lambda _: g.b("it_fsm") == 1 and not g.fs_owed(), what,
            poll=0.1, guest=40)
    M.guest_sleep(g.m, 0.25)


def fs_enter(ui, g, how):
    """INTO full screen by F, Alt+Enter or the Game menu; returns once the
    bracket has made its first whole present."""
    m = g.m
    n0 = g.w("it_fsn")
    if how == "F":
        m.key("KeyF")
    elif how == "alt":
        m.alt("Enter", hold=.15)
    elif how == "menu":
        ui.menu_pick("Game", "Full Screen")
    else:
        raise ValueError(how)
    M.until(m, lambda _: g.b("it_fsm") == 1 and g.w("it_fsn") > n0,
            "full screen by %s" % how, poll=0.1, guest=40)
    fs_settled(g)


def fs_leave(ui, g, how):
    """OUT of full screen by F, Alt+Enter or Esc (at the title); returns with
    the window's picture presented."""
    m = g.m
    n0 = g.w("it_fsout")
    if how == "F":
        m.key("KeyF")
    elif how == "alt":
        m.alt("Enter", hold=.15)
    elif how == "esc":
        m.key("Escape")
    else:
        raise ValueError(how)
    # it_fsout moves when OSAPI_FSX_RUN has RETURNED - the kernel's exit mode
    # set and its wm_paint_all done (SPEC.md 53.6); it_fsm drops before both
    M.until(m, lambda _: g.b("it_fsm") == 0 and g.w("it_fsout") > n0,
            "out of full screen by %s" % how, poll=0.1, guest=40)
    presented(ui, g)


def guest_str(name):
    """A string the package shows, read off its source (isotron.asm's
    `name: db '...', 0`) - a second copy here would drift."""
    import re
    src = open(os.path.join(ROOT, "apps", "isotron", "isotron.asm"), encoding="utf-8").read()
    mt = re.search(r"^%s:\s+db\s+'([^']*)',\s*0" % name, src, re.M)
    assert mt, "isotron.asm has no string %s" % name
    return mt.group(1)


def fs_want(state, sp=0, sc=0, winner=None):
    """The overlay records the state shows, as fs_overlays() gives them -
    (row, c0, cells, text) in order left HUD, message, right HUD - built
    from SPEC.md 108.12.5's places and the model's state, never read off the
    guest: `state` is "title", "play" (the HUD alone), "wait" (a round's
    result, `winner` P or C) or "over"."""
    recs = []
    left, right = "PLAYER %d" % sp, "COMPUTER %d" % sc
    msg = {"title": guest_str("it_s_title"), "play": None,
           "over": guest_str("it_s_over"),
           "wait": guest_str("it_s_pwin" if winner == "P" else "it_s_cwin")}[state]
    if state != "title":
        recs.append((0, 2, len(left), left))
    if msg:
        msg = msg[:S.FS_CELLS]
        recs.append((23, (S.FS_CELLS - len(msg)) // 2, len(msg), msg))
    if state != "title":
        recs.append((24, S.FS_CELLS - 1 - len(right), len(right), right))
    return recs


def fs_want_rec(rec):
    """fs_want for the pause after a model round (S.Game.play_round's rec)."""
    return fs_want("over" if rec["lossrow"] >= 3 else "wait", rec["sp"], rec["sc"],
                   rec["winner"])


def fs_texts(g, tag):
    """The lines the bracket shows, for a person (EGA's are the windowed
    overlays', 108.12.5)."""
    if tag == "ega":
        return [t for _, _, _, _, t in g.overlays()]
    return [t for _, _, _, t in g.fs_overlays()]


def fs_glyphs(g):
    """The 8 x 8 glyph table the bracket letters from (OSAPI_FONT_GLYPHS, as
    it_fs_setup kept it): ch -> its 8 row bytes, a blank cell outside it."""
    first, last = g.b("it_gfirst"), g.b("it_glast")
    assert 0 < first <= last, "no glyph table kept (it_gfirst %d, it_glast %d)" % (first, last)
    tab = bytes(g.m.read((g.w("it_gseg") << 4) + g.w("it_goff"), 8 * (last - first + 1)))

    def gl(ch):
        k = ord(ch) - first
        return tab[8 * k:8 * k + 8] if 0 <= ord(ch) <= last and k >= 0 else bytes(8)
    return gl


NIB2 = [sum(3 << (2 * b) for b in range(4) if (v >> b) & 1) for v in range(16)]


def fs_glass(g, tag, canvas, what, name=None, look="isotron", hi=False, want=None):
    """The bracket's glass against the model's full-screen presenter of the
    same canvas WITH ITS TEXT: the CGA's and the Hercules' VIDEO MEMORY byte
    for byte, the VGA's (0Dh) and the EGA's rendering pixel for pixel, every
    unit of the picture and none skipped. The text cells' expected bytes are
    the glyphs of `want` - fs_want's records, the model's state - at SPEC.md
    108.12.5's places (EGA: the windowed overlays' places at Hc 350, font_run
    white on black), drawn from the guest's glyph table; and the guest's own
    records must BE `want`. Returns the units compared."""
    m = g.m
    assert g.b("it_fsk") == L.FSK[tag], "%s %s: the bracket's surface is %d, not %d" \
        % (tag, what, g.b("it_fsk"), L.FSK[tag])
    assert want is not None, "fs_glass needs the text the state shows"
    lk = S.LOOKS[look]
    gl = fs_glyphs(g)
    m.pause()
    try:
        ovs = g.fs_overlays()
        wovs = g.overlays() if tag == "ega" else None
        if tag == "cga":
            raw = bytes(m.read(0xB8000, 0x4000))
            fw, fh, fb = m.fbuf(0)
        elif tag == "herc":
            raw = bytes(m.read(0xB0000, 0x8000))
            fw, fh, fb = m.fbuf(0)
        else:
            fw, fh, fb = m.fbuf(0)
            raw = None
    finally:
        m.run()
    if REPORTS and name:
        M.write_png_rgb(os.path.join(REPORTS, name + ".png"), fw, fh, fb)
    if tag == "ega":
        # EGA letters through font_run at the WINDOWED overlays' places
        # (108.12.5): the same strings, the left from byte 1, the right to
        # byte pwb - 2, the message centred - at Hc 350's rows
        pwb = g.w("it_pwb")
        got_t = [(x0 // 8, t) for x0, _, _, _, t in wovs]
        want_t = []
        for k, (row, _, n, t) in enumerate(want):
            j0 = 1 if row == 0 else (pwb - 1 - n if row == 24 else (pwb - n) // 2)
            want_t.append((j0, t))
        assert got_t == want_t, "%s %s: the overlays are %r, the state shows %r" \
            % (tag, what, got_t, want_t)
    else:
        assert ovs == want, "%s %s: the full-screen text records are %r, the state " \
            "shows %r" % (tag, what, ovs, want)
    n = bad = 0
    first = None
    if tag in ("cga", "herc"):
        want_m = bytearray(S.cga320(canvas) if tag == "cga" else S.herc(canvas))
        for row, c0, k, txt in want:
            for i, ch in enumerate(txt):
                gb = gl(ch)
                for gy in range(8):
                    y = 8 * row + gy
                    pair = (NIB2[gb[gy] >> 4], NIB2[gb[gy] & 15])
                    rs = [S.cga_off(y)] if tag == "cga" else \
                        [S.herc_off(r) for r in range(S.herc_c(y), S.herc_c(y + 1))]
                    for o in rs:
                        want_m[o + 2 * (c0 + i)], want_m[o + 2 * (c0 + i) + 1] = pair
        for i in range(len(want_m)):
            n += 1
            if raw[i] != want_m[i]:
                bad += 1
                if first is None:
                    first = (i, raw[i], want_m[i])
        assert not bad, "%s %s: %d of %d video bytes differ from the model's presenter and " \
            "its text; first at offset %04x: %02x, the model's %02x" % ((tag, what, bad, n) + first)
        if tag == "cga":
            # PALETTE 1, the intensity asked for: the card's own rendering
            # holds exactly those inks (the BIOS's AH=0Bh, never port 3D9h)
            pal = set(tuple(fb[i:i + 3]) for i in range(0, len(fb), 3))
            allowed = set(S.CGA_RGB[hi])
            assert pal <= allowed and S.CGA_RGB[hi][1] in pal and S.CGA_RGB[hi][2] in pal, \
                "%s %s: the CGA shows %r - not palette 1 at %s intensity" \
                % (tag, what, sorted(pal), "high" if hi else "low")
        return n
    f = nearest(S.EGA_RGB)
    if tag == "vga":
        assert (fw, fh) == (640, 400), ("the 0Dh rendering is line-doubled", fw, fh)
        got = [bytes(f(tuple(fb[((2 * y) * fw + 2 * x) * 3:((2 * y) * fw + 2 * x) * 3 + 3]))
                     for x in range(S.CW)) for y in range(S.CH)]
        want_p = [bytearray(r) for r in S.vga0d(canvas, lk)]
        for row, c0, k, txt in want:
            for i, ch in enumerate(txt):
                gb = gl(ch)
                for gy in range(8):
                    for b in range(8):
                        want_p[8 * row + gy][8 * (c0 + i) + b] = 15 if gb[gy] & (0x80 >> b) else 0
        W, H = S.CW, S.CH
    else:
        assert (fw, fh) == (640, 350), ("the EGA desktop is 640 x 350", fw, fh)
        geo = g.geom()
        ox, oy = geo["ox"], geo["oy"]
        got = [bytes(f(tuple(fb[(y * fw + x) * 3:(y * fw + x) * 3 + 3]))
                     for x in range(fw)) for y in range(fh)]
        got = [r[ox:ox + S.WIN_W] for r in got[oy:oy + S.EGA_HC]]
        want_p = [bytearray(r) for r in S.ega(canvas, lk)]
        for x0, y0, _, _, txt in wovs:
            for i, ch in enumerate(txt):
                gb = gl(ch)
                for gy in range(8):
                    for b in range(8):
                        want_p[y0 + gy][x0 + 8 * i + b] = 15 if gb[gy] & (0x80 >> b) else 0
        W, H = S.WIN_W, S.EGA_HC
    for y in range(H):
        for x in range(W):
            n += 1
            if got[y][x] != want_p[y][x]:
                bad += 1
                if first is None:
                    first = (x, y, got[y][x], want_p[y][x])
    assert not bad, "%s %s: %d of %d pixels differ from the model's presenter and its " \
        "text; first at (%d, %d): glass %d, model %d" % ((tag, what, bad, n) + first)
    return n


def fs_leg(tag, rounds=3):
    """isotronfs (and isotronega's full-screen half): F at the title, the
    title's canvas on the glass, then a seeded game's rounds - after each the
    canvas the model's and the glass the model's full-screen presenter of it;
    then C (high intensity on a CGA, Classic on a 16-colour surface) and back;
    then Esc in a round ends the game and stays full screen, and Esc at the
    title leaves."""
    system, apps, machine = boot_for(tag)
    seed, tier = S.STORED[0][0], S.STORED[0][1]
    with os88ui.boot(system, apps=apps, machine=FS_MACHINES[tag]) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        fs_enter(ui, g, "F")
        print("  %s: full screen, surface %d, %d whole present(s)"
              % (tag, g.b("it_fsk"), g.w("it_fsn")), flush=True)
        tg, tc = S.Grid(), S.Canvas()
        tc.title(tg)
        assert g.canvas() == tc.pack(), "the title's canvas differs from the model's"
        n = fs_glass(g, tag, tc, "title", "w3-fs-%s-title" % tag, want=fs_want("title"))
        print("    title: %d units equal the model's and its text; overlays %r"
              % (n, fs_texts(g, tag)), flush=True)
        model = S.Game(seed, tier, canvas=True)
        start_game(g, seed, tier, L.TF_FAST | L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        prev = None
        for k in range(rounds):
            rec = model.play_round()
            M.until(m, lambda _: g.b("it_tpause") == 1, "round %d to end" % (k + 1),
                    poll=0.2, guest=240)
            fs_settled(g)
            st = g.state()
            assert (st["h"], st["ck"], st["iter"]) == (rec["h"], rec["ck"], rec["iter"]), \
                "round %d: the guest played a different round (%r)" % (k + 1, st)
            assert g.canvas() == model.canvas.pack(), "round %d: the canvas differs" % (k + 1)
            want = fs_want_rec(rec)
            n = fs_glass(g, tag, model.canvas, "round %d" % (k + 1),
                         "w3-fs-%s-r%d" % (tag, k + 1), want=want)
            print("    round %d (h%d, %s): %d units equal the model's and its text; "
                  "overlays %r" % (k + 1, rec["h"], rec["winner"], n, fs_texts(g, tag)),
                  flush=True)
            if prev is not None:
                # THE NEGATIVE CONTROLS: the same comparison against the LAST
                # round's canvas, and against this one with the score one
                # off, must both fail - or this row could pass a glass that
                # never moved, or text in the right place saying the wrong thing
                for cv_, w_, why in ((prev, want, "the previous round's canvas"),
                                     (model.canvas, fs_want_rec(dict(rec, sp=rec["sp"] + 1)),
                                      "a score one off")):
                    try:
                        fs_glass(g, tag, cv_, "negative control", want=w_)
                    except AssertionError as e:
                        assert "differ" in str(e) or "records are" in str(e) or \
                            "overlays are" in str(e), "%s: refused for another reason: %s" \
                            % (why, e)
                    else:
                        raise AssertionError("%s round %d: the glass check passed against %s"
                                             % (tag, k + 1, why))
                print("    round %d: the check refuses the previous round's canvas and a "
                      "score one off" % (k + 1), flush=True)
            prev = S.Canvas.unpack(model.canvas.pack())
            if k + 1 < rounds:
                m.pause()
                g.put("it_tpause", 0)
                m.run()
        # --- C: the look, a present and never a redraw ----------------------------
        if tag != "herc":
            n0 = g.w("it_fsn")
            m.key("KeyC")
            if tag == "cga":
                M.until(m, lambda _: g.b("it_cgahi") == 1, "C: high intensity", guest=10)
                fs_settled(g)
                assert g.w("it_fsn") == n0, "C on a CGA re-presented the picture"
                fs_glass(g, tag, model.canvas, "C (high intensity)",
                         "w3-fs-%s-hi" % tag, hi=True, want=want)
            else:
                M.until(m, lambda _: g.b("it_clook") == 1 and g.w("it_fsn") > n0,
                        "C: the Classic look", guest=40)
                fs_settled(g)
                fs_glass(g, tag, model.canvas, "C (Classic)", "w3-fs-%s-classic" % tag,
                         look="classic", want=want)
            assert g.canvas() == model.canvas.pack(), "C changed the canvas"
            n1 = g.w("it_fsn")
            m.key("KeyC")
            M.until(m, lambda _: g.b("it_cgahi") == 0 and g.b("it_clook") == 0 and
                    (tag == "cga" or g.w("it_fsn") > n1), "C again: the first look", guest=40)
            fs_settled(g)
            fs_glass(g, tag, model.canvas, "C twice", want=want)
            print("    C: %s and back, the canvas untouched"
                  % ("high intensity" if tag == "cga" else "Classic"), flush=True)
        # --- Esc: the game first, then the full screen (SPEC.md 11.2.1) -----------
        m.pause()
        g.put("it_tflags", 0)
        g.put("it_tpause", 0)
        m.run()
        n0 = g.w("it_fsn")
        m.key("Escape")
        M.until(m, lambda _: g.b("it_state") == L.ST_TITLE and g.w("it_fsn") != n0,
                "Esc to end the game (and the title's present)", guest=20)
        fs_settled(g)
        assert g.b("it_fsm") == 1, "Esc in a game left full screen - it must end the game first"
        tg, tc = S.Grid(), S.Canvas()
        tc.title(tg)
        fs_glass(g, tag, tc, "the title after Esc", "w3-fs-%s-esc" % tag,
                 want=fs_want("title"))
        fs_leave(ui, g, "esc")
        assert g.b("it_state") == L.ST_TITLE
        geo = g.geom()
        check_glass(g, "vga" if tag == "ega" else tag, tc, "windowed after Esc")
        print("    Esc: ended the game in full screen; Esc at the title left it; the "
              "window's picture is the title (%dx%d content)" % (geo["cw"], geo["ch"]),
              flush=True)
    return 0


def snap(g):
    """Everything a toggle must leave alone: the game's words, the grid, the
    canvas."""
    m = g.m
    m.pause()
    try:
        out = dict(iter=g.w("it_iter"), rng=g.w("it_rng"), ck=g.w("it_ck"),
                   sp=g.w("it_sp"), sc=g.w("it_sc"), h=g.b("it_h"),
                   nsteps=g.w("it_nsteps"), state=g.b("it_state"),
                   grid=bytes(m.read(g.w("it_seg") << 4, 217 * 101)),
                   canvas=bytes(g.canvas()))
    finally:
        m.run()
    return out


def toggle_leg(tag):
    """isotrontoggle: mid-round, the game frozen by the harness (it_tpause -
    no iteration, no pause message), F, Alt+Enter and the Game menu into full
    screen and F, Alt+Enter out, twice over: the iteration, the RNG, the
    checksum, the grid and the canvas the same bytes after every toggle, and
    the glass right on both sides. Then the round is released and must still
    end as the model's did; a second round toggles WHILE IT RUNS and must
    too. Esc's four cases (SPEC.md 108.12.6)."""
    system, apps, machine = boot_for(tag)
    seed, tier = S.STORED[0][0], S.STORED[0][1]
    with os88ui.boot(system, apps=apps, machine=FS_MACHINES[tag]) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        # windowed at the title, Esc does nothing
        m.key("Escape")
        M.guest_sleep(m, 0.5)
        assert g.b("it_state") == L.ST_TITLE and g.b("it_fsm") == 0, "Esc at the windowed title"
        model = S.Game(seed, tier, canvas=True)
        rec1 = model.play_round()
        # A PACED game (no FAST): BOT | NOFLOW | HOLD, frozen mid-round
        start_game(g, seed, tier, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and g.w("it_iter") > 120,
                "the round under way", poll=0.1, guest=60)
        m.pause()
        g.put("it_tpause", 1)               # the harness's hold: no iteration
        m.run()
        presented(ui, g)
        s0 = snap(g)
        assert 0 < s0["iter"] < rec1["iter"], "the round is not mid-way (%d of %d)" \
            % (s0["iter"], rec1["iter"])
        cv = S.Canvas.unpack(s0["canvas"])
        wt = "vga" if tag == "vga" else tag

        def same(where):
            s1 = snap(g)
            bad = [k for k in s0 if s0[k] != s1[k]]
            assert not bad, "%s: the toggle changed %s" % (where, ", ".join(bad))
        n = 0
        for lap in (1, 2):
            for into, out in (("F", "F"), ("alt", "alt"), ("menu", "F")):
                fs_enter(ui, g, into)
                same("into full screen by %s (lap %d)" % (into, lap))
                fs_glass(g, tag, cv, "mid-round, in by %s" % into,
                         "w3-tg-%s-fs-%s%d" % (tag, into, lap) if lap == 1 else None,
                         want=fs_want("play"))
                fs_leave(ui, g, out)
                same("out of full screen by %s (lap %d)" % (out, lap))
                check_glass(g, wt, cv, "mid-round, out by %s" % out,
                            "w3-tg-%s-win-%s%d" % (tag, out, lap) if lap == 1 else None)
                n += 2
        print("  %s: %d toggles mid-round (F, Alt+Enter, the menu in; F, Alt+Enter out; "
              "twice), iteration %d, rng %04x, ck %04x, the grid and the canvas the same "
              "bytes after every one, the glass the model's on both sides"
              % (tag, n, s0["iter"], s0["rng"], s0["ck"]), flush=True)
        # released: the round ends as the model's did
        m.pause()
        g.put("it_tpause", 0)
        m.run()
        M.until(m, lambda _: g.b("it_tpause") == 1, "round 1 to end", poll=0.2, guest=120)
        st = g.state()
        assert (st["winner"], st["iter"], st["rng"], st["ck"]) == \
            (rec1["winner"], rec1["iter"], rec1["rng"], rec1["ck"]), \
            "round 1 after the toggles: %r, the model's %r" % (st, rec1)
        # THE NEGATIVE CONTROL: the same comparison across a RELEASED stretch
        # must fail, or `same` could not see a toggle that moved the game
        try:
            same("the released round")
        except AssertionError:
            pass
        else:
            raise AssertionError("%s: snap() saw nothing change across a played round" % tag)
        # round 2: toggles WHILE IT RUNS (no hold), in full screen at its end
        rec2 = model.play_round()
        m.pause()
        g.put("it_tpause", 0)
        m.run()
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and g.w("it_round") == 1
                and g.w("it_iter") > 60, "round 2 under way", poll=0.1, guest=60)
        base = paced_wakes(g, 40)
        fs_enter(ui, g, "F")
        M.guest_sleep(m, 1.0)
        it_a = g.w("it_iter")
        back = back_wakes(ui, g)
        # V = NOW ACROSS THE WAY BACK (SPEC.md 108.4.2, 108.12.6): the worker's
        # first wake that iterates after it owes nothing for the mode set and
        # the repaint - at most what a paced wake runs. A clock synced inside
        # the bracket, before them, owed DEBT_CAP: two ticks in one wake
        assert back["first"] is not None, "%s: no wake iterated after the way back" % tag
        assert back["first"] <= base["med"], "%s: the first wake after leaving full screen " \
            "ran %d iterations, a paced wake %d (median of %d) - the way back was owed" \
            % (tag, back["first"], base["med"], base["n"])
        print("  %s: after the way back the worker's first iterating wake (its %d%s) ran %d "
              "iteration(s); a paced wake runs %d (median of %d, at most %d)"
              % (tag, back["k"], "th" if back["k"] % 100 in (11, 12, 13) else
                 {1: "st", 2: "nd", 3: "rd"}.get(back["k"] % 10, "th"),
                 back["first"], base["med"], base["n"], base["max"]), flush=True)
        M.guest_sleep(m, 1.0)
        fs_enter(ui, g, "alt")
        M.until(m, lambda _: g.b("it_tpause") == 1, "round 2 to end", poll=0.2, guest=120)
        fs_settled(g)
        st = g.state()
        assert (st["winner"], st["iter"], st["rng"], st["ck"]) == \
            (rec2["winner"], rec2["iter"], rec2["rng"], rec2["ck"]), \
            "round 2 toggled while running: %r, the model's %r" % (st, rec2)
        assert g.canvas() == model.canvas.pack(), "round 2: the canvas differs"
        fs_glass(g, tag, model.canvas, "round 2's end", want=fs_want_rec(rec2))
        print("  %s: round 1 released after them ends as the model's (it %d, %s); round 2 "
              "toggled while running (in at it ~%d) ends as the model's too (it %d, %s)"
              % (tag, rec1["iter"], rec1["winner"], it_a, rec2["iter"], rec2["winner"]),
              flush=True)
        # Esc: in a round in full screen it ends the game and stays
        m.pause()
        g.put("it_tflags", 0)
        g.put("it_tpause", 0)
        m.run()
        n0 = g.w("it_fsn")
        m.key("Escape")
        M.until(m, lambda _: g.b("it_state") == L.ST_TITLE and g.w("it_fsn") != n0,
                "Esc in full screen (and the title's present)", guest=20)
        fs_settled(g)
        assert g.b("it_fsm") == 1, "Esc in a full-screen game left full screen"
        fs_leave(ui, g, "esc")             # ...and at the title it leaves
        # windowed, in a round, Esc ends the game
        start_game(g, seed, tier, L.TF_BOT | L.TF_NOFLOW)
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY, "a round", guest=30)
        m.key("Escape")
        M.until(m, lambda _: g.b("it_state") == L.ST_TITLE, "Esc in a windowed round",
                guest=20)
        assert g.b("it_fsm") == 0
        print("  %s: Esc - windowed title: nothing; windowed round: ends the game; full-"
              "screen round: ends the game, stays full screen; full-screen title: leaves"
              % tag, flush=True)
    return 0


def paced_wakes(g, n):
    """Iterations per worker wake over `n` wakes of a paced round in play:
    stats of iter(it_wake1) - iter(it_wake0)."""
    m = g.m
    W0, W1 = g.base + g.code["it_wake0"], g.base + g.code["it_wake1"]
    ia, sa = g.a("it_iter"), g.a("it_state")

    def onhit(mm, rec):
        return (struct.unpack("<H", bytes(mm.read(ia, 2)))[0], mm.read(sa, 1)[0])
    with M.bp_trace(m, W0, W1, cap=2 * n + 10, on_hit=onhit) as tr:
        tr.wait(2 * n, limit=120)
    per, last = [], None
    for h in tr.hits:
        if h["addr"] == W0:
            last = h
        elif h["addr"] == W1 and last is not None:
            if last["hit"][1] == L.ST_PLAY and h["hit"][1] == L.ST_PLAY:
                per.append(h["hit"][0] - last["hit"][0])
            last = None
    return stats(per)


def back_wakes(ui, g, key="alt"):
    """Leave full screen by `key` in a round in play, tracing the way back and
    the worker's wakes after it until four iterations have run: the
    iterations of the FIRST wake after OSAPI_FSX_RUN returned that ran any,
    and which wake that was."""
    m = g.m
    W0, W1 = g.base + g.code["it_wake0"], g.base + g.code["it_wake1"]
    XB = g.base + g.code["it_fsback"]
    ia = g.a("it_iter")

    def onhit(mm, rec):
        return struct.unpack("<H", bytes(mm.read(ia, 2)))[0]
    n0 = g.w("it_fsout")
    with M.bp_trace(m, XB, W0, W1, cap=4000, on_hit=onhit) as tr:
        if key == "alt":
            m.alt("Enter", hold=.15)
        else:
            m.key("KeyF")
        tr.until(lambda: g.b("it_fsm") == 0 and g.w("it_fsout") > n0, "the way back",
                 limit=120)
        i1 = g.w("it_iter")
        tr.until(lambda: g.w("it_iter") >= i1 + 4, "four iterations after the way back",
                 limit=120)
    assert not tr.overflowed, "the way back's trace overflowed"
    hs = tr.hits
    ixb = next(i for i, h in enumerate(hs) if h["addr"] == XB)
    # the worker can be parked in OSAPI_GFX_LOCK from BEFORE the way back -
    # it_wake0 is before the lock - and that wake runs after it: count it
    i0 = ixb + 1
    prev = [i for i in range(ixb) if hs[i]["addr"] in (W0, W1)]
    if prev and hs[prev[-1]]["addr"] == W0:
        i0 = prev[-1]
    k, first, w0 = 0, None, None
    for h in hs[i0:]:
        if h["addr"] == W0:
            w0 = h
        elif h["addr"] == W1 and w0 is not None:
            k += 1
            if h["hit"] != w0["hit"]:
                first = h["hit"] - w0["hit"]
                break
            w0 = None
    presented(ui, g)
    return dict(first=first, k=k)


def herc_rows(raw):
    """A Hercules' 32 KB as (720, 348, rows of bits), for a picture."""
    rows = []
    for y in range(348):
        o = (y & 3) * 0x2000 + (y >> 2) * 90
        rows.append(bytearray((raw[o + (x >> 3)] >> (7 - (x & 7))) & 1 for x in range(720)))
    return 720, 348, rows


def twodisp_leg():
    """isotrontwodisp: MartyPC's XT with a VGA and a Hercules, the desktop
    extended right. The window on the VGA is colour; dragged wholly onto the
    Hercules it is mono after EXACTLY ONE whole present; F there is the
    FSXM_HERC bracket on the Hercules, and the VGA's picture and raster are
    left exactly as they were; Esc at the title comes home."""
    import dispcp
    import os88mouse
    import os88sym
    Sy = os88sym.linear
    apps = L.fresh_disk(os88build.at("build/isotron360.img"))
    with M.launch(os88build.at("build/os8088-360.img"), apps=apps,
                  machine="os8088_xt_vga_herc", boot=False) as m:
        cards = m.cards()
        assert len(cards) == 2, "os8088_xt_vga_herc is not a two-card machine"
        m.run()
        M.settle(m, gate=M.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo, verbose=False)
        dispcp.open_panel(m, mo, Sy, M.settle)
        dispcp.set_mode(m, mo, Sy, M.settle, "right")
        dispcp.close_panel(m, mo, Sy, M.settle)
        assert m.read(Sy("vid_ndisp"), 1)[0] == 2, "the desktop did not extend"
        g = L.open_game(ui)
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        geo = g.geom()
        assert geo["surf"] == 1, "on the VGA the window is not colour (%r)" % geo
        # ACROSS THE SEAM: the window's display is then the RESTRICTIVE card's,
        # the Hercules (SPEC.md 39.16.4.2), and the kernel refits the frame
        # to it (wm_strad_fit: 722 x 300) - a 640 x 420 frame cannot land
        # wholly on a 348-row display, and the refit one is wider than 720
        win = ui.window(L.TITLE)
        n0 = g.w("it_nwhole")
        x, y, w, h = dispcp.win_rect(m, Sy, win.i)
        mo.drag(x + w // 2, y + 8, 320 + w // 2, y + 8)
        M.settle(m)
        ui.mo.to(2, 2)
        M.until(m, lambda _: not g.owed(), "the picture across the seam", poll=0.1, guest=60)
        M.guest_sleep(m, 0.5)
        geo = g.geom()
        whole = g.w("it_nwhole") - n0
        print("  dragged across the seam to %r: content at (%d,%d) %dx%d, surface %s, bpp %d; "
              "%d whole present(s)" % (dispcp.win_rect(m, Sy, win.i), geo["cx"], geo["cy"],
                                       geo["cw"], geo["ch"],
                                       "colour" if geo["surf"] else "mono", geo["bpp"], whole),
              flush=True)
        assert geo["surf"] == 0 and geo["bpp"] == 1, "across the seam the window is not mono"
        assert whole == 1, "the format switch made %d whole presents, not one" % whole
        # the VGA, before: its rendering, and its memory (MartyPC's peek of
        # A000 reads plane 0 - tests/n1942.py), 38,400 bytes of 12h
        m.pause()
        v0 = m.fbuf(0)
        p0 = bytes(m.read(0xA0000, 38400))
        m.run()
        n1 = g.w("it_fsn")
        m.key("KeyF")
        M.until(m, lambda _: g.b("it_fsm") == 1 and g.w("it_fsn") > n1, "the Hercules bracket",
                poll=0.1, guest=40)
        fs_settled(g)
        assert g.b("it_fsk") == L.FSK["herc"], "the bracket is surface %d, not the Hercules'" \
            % g.b("it_fsk")
        m.pause()
        v1 = m.fbuf(0)
        p1 = bytes(m.read(0xA0000, 38400))
        raw = bytes(m.read(0xB0000, 0x8000))
        m.run()
        assert (v1[0], v1[1]) == (v0[0], v0[1]), "the VGA's raster changed: %dx%d, was %dx%d" \
            % (v1[0], v1[1], v0[0], v0[1])
        # (below the menu bar: the arrow parked at (2, 2) comes off at the lock,
        # and the bar's clock is live - tests/dispfsx.py skips it for that)
        moved = [i for i in range(len(p0)) if p0[i] != p1[i]]
        dp = sum(1 for i in moved if i >= 20 * 80)
        assert dp == 0, "the Hercules bracket wrote %d bytes of the VGA's memory below " \
            "its menu bar (first at row %d)" % (dp, min(i for i in moved if i >= 1600) // 80)
        lit = sum(1 for i in range(0, len(v1[2]), 3) if v1[2][i:i + 3] != b"\0\0\0")
        if REPORTS:
            M.write_png_rgb(os.path.join(REPORTS, "w3-2d-vga-before.png"), *v0)
            M.write_png_rgb(os.path.join(REPORTS, "w3-2d-vga-during.png"), *v1)
            M.write_png(os.path.join(REPORTS, "w3-2d-herc-during.png"), *herc_rows(raw))
        # THE VGA IS DARK, BY DESIGN: a mode-setting bracket makes the machine
        # one display and blanks the others (SPEC.md 39.18 - two outs, the
        # sync kept, the framebuffer untouched); what this bracket must not do
        # is write it or change its mode, which the two checks above hold
        assert lit == 0, "the VGA shows %d lit pixels during a Hercules bracket - " \
            "SPEC.md 39.18 blanks it" % lit
        tg, tc = S.Grid(), S.Canvas()
        tc.title(tg)
        want = S.herc(tc)
        cells = set()
        for row, c0, k, _ in g.fs_overlays():
            for r, x0, x1 in S.fs_cells("herc", row, c0, k):
                cells.update(range(S.herc_off(r) + x0, S.herc_off(r) + x1))
        bad = sum(1 for i in range(len(want)) if i not in cells and raw[i] != want[i])
        assert bad == 0, "the Hercules bracket's video memory differs from the model in %d " \
            "bytes" % bad
        print("  F on the Hercules: the FSXM_HERC bracket, its 32 KB the model's; the VGA "
              "still %dx%d, its memory (plane 0, 38,400 bytes) unwritten, and dark as "
              "SPEC.md 39.18 makes the other display" % (v1[0], v1[1]), flush=True)
        fs_leave(ui, g, "esc")
        assert g.b("it_fsm") == 0
        m.pause()
        v2 = m.fbuf(0)
        m.run()
        assert (v2[0], v2[1]) == (v0[0], v0[1]), "the VGA's raster after the bracket"
        lit2 = sum(1 for i in range(0, len(v2[2]), 3) if v2[2][i:i + 3] != b"\0\0\0")
        assert lit2 > 10000, "the VGA did not come back after the bracket"
        print("  Esc at the title: home, the VGA %dx%d and lit again (%d pixels)"
              % (v2[0], v2[1], lit2), flush=True)
    return 0


# --- isotronperf, the full-screen part (SPEC.md 108.12.7) ----------------------
FS_CEIL = {"step": {"cga": 4500, "vga": 4000, "herc": 5000},   # ISOTRON-PLAN 16
           "present_ms": 1500.0,        # a whole present on toggle
           "first_light_ms": 1500.0}    # the way back, to the window's first band


def fs_tile(lab, rects, ovs):
    """A whole present's rects (fy0, fy1, fc0, fc1), as the presenters were
    entered, must tile the 200 rows x 40 cells EXACTLY once outside the text
    cells and never inside them (SPEC.md 108.12.5): a presenter that hands
    it_fs_whole back a wrong cursor presents rows twice - which the VGA's did,
    BP = 0 after it_fr_vga, rows [0, 200) again under the last overlay."""
    text = set((y, c) for row, c0, k, _ in ovs for y in range(8 * row, 8 * row + 8)
               for c in range(c0, c0 + k))
    cover = set()
    for y0, y1, c0, c1 in rects:
        assert y0 % 2 == 0 and y1 % 2 == 0, "%s: a rect of rows [%d, %d) - not in pairs" \
            % (lab, y0, y1)
        for y in range(y0, y1):
            for c in range(c0, c1):
                assert (y, c) not in cover, "%s: row %d cell %d presented twice (rects %r)" \
                    % (lab, y, c, rects)
                assert (y, c) not in text, "%s: row %d cell %d is under text and was " \
                    "presented" % (lab, y, c)
                cover.add((y, c))
    assert len(cover) + len(text) == S.CH * S.FS_CELLS, "%s: %d cells presented and %d of " \
        "text, of %d" % (lab, len(cover), len(text), S.CH * S.FS_CELLS)
    return len(cover)


def fs_perf_leg(tag, out):
    """The head-step WITH its span hook - RNG, decision, move, canvas and the
    column written into video memory - over EVERY computer head-step of the
    same whole rounds as the windowed line (PERF_ROUNDS), in the bracket; the
    whole present F makes at the title AND in a round (its rects tiling the
    picture exactly once outside the text); and the way back - the bracket's
    last frame to OSAPI_FSX_RUN's return, which is the window's first light,
    and then the worker's holds that put the picture out in bands. Median
    against ISOTRON-PLAN section 16's ceiling - a stricter statistic than the
    plan's minimum of 25 samples, which is at most the median; the minimum
    over the whole sample is reported - the presents and first light against
    1.5 s, each band's hold against 50 ms. EGA (the VIDEO=ega tree on the
    XT-VGA - the instruction count and gfx_blitp's path are the EGA's, only
    the card's wait states differ) has no span hook to time: its lines are
    the presents, the way back, and a Lunatic h4 frame with the governor."""
    system, apps, machine = boot_for(tag)
    res = {}
    ms = lambda c: 1000.0 * c / M.GUEST_HZ       # noqa: E731
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        presented(ui, g)
        B = g.base
        HS0, HS1 = B + g.code["it_hs0"], B + g.code["it_hs1"]
        AI1 = B + g.code["it_ai1"]
        F0, F1 = B + g.code["it_fsw0"], B + g.code["it_fsw1"]
        RC = [B + g.code[k] for k in ("it_fr_cga", "it_fr_vga", "it_fr_herc")]
        X0, XB = B + g.code["it_fsx0"], B + g.code["it_fsback"]
        W0, W1, FD = B + g.code["it_wake0"], B + g.code["it_wake1"], B + g.code["it_fdone"]
        FY = g.a("it_fy0")              # it_fy0, it_fy1, it_fc0, it_fc1: four words

        def rect_hit(mm, rec):
            return struct.unpack("<4H", bytes(mm.read(FY, 8)))

        def whole(lab, trigger):
            """F's whole present: its cycles, and its rects tiled."""
            n0 = g.w("it_fsn")
            with M.bp_trace(m, F0, F1, *RC, cap=400, on_hit=rect_hit) as tr:
                trigger()
                tr.until(lambda: g.w("it_fsn") > n0, "the %s whole present" % lab, limit=180)
            assert not tr.overflowed, "%s: the trace overflowed" % lab
            hs = tr.hits
            i0 = next(i for i, h in enumerate(hs) if h["addr"] == F0)
            i1 = next(i for i in range(i0 + 1, len(hs)) if hs[i]["addr"] == F1)
            c = hs[i1]["cycles"] - hs[i0]["cycles"]
            rects = [hs[i]["hit"] for i in range(i0, i1) if hs[i]["addr"] in RC]
            fs_settled(g)
            r = dict(cycles=c, ms=ms(c), rects=len(rects))
            if tag != "ega":
                r["cells"] = fs_tile("%s %s" % (tag, lab), rects, g.fs_overlays())
            res["fs_present_" + lab] = r
            print("  %s: F's whole present (%s) %d cycles = %.0f ms%s" % (
                tag, lab, c, r["ms"], "" if tag == "ega" else
                "; %d rects tile %d cells exactly once outside the text" % (
                    len(rects), r["cells"])), flush=True)
            return r

        def way_back(lab, key):
            """First light (it_fsx0 .. it_fsback) and the band holds after it."""
            n0 = g.w("it_fsout")
            with M.bp_trace(m, X0, XB, W0, W1, FD, cap=4000) as tr:
                m.key(key)
                tr.until(lambda: g.w("it_fsout") > n0, "the way back (%s)" % lab, limit=180)
                tr.until(lambda: not g.owed(), "the window's picture (%s)" % lab, limit=180)
            assert not tr.overflowed, "%s: the trace overflowed" % lab
            hs = tr.hits
            bk = pairs(hs, X0, XB)
            assert bk, "the way back was not traced"
            ixb = next(i for i, h in enumerate(hs) if h["addr"] == XB)
            ifd = next((i for i in range(ixb, len(hs)) if hs[i]["addr"] == FD), None)
            assert ifd is not None, "%s: no banded whole present after the way back" % lab
            holds, cur = [], None
            for i in range(ixb + 1, len(hs)):
                if hs[i]["addr"] == W0:
                    cur = hs[i]["cycles"]
                elif hs[i]["addr"] == W1 and cur is not None:
                    holds.append(hs[i]["cycles"] - cur)
                    cur = None
                    if i > ifd:
                        break
            r = dict(cycles=bk[0][0], ms=ms(bk[0][0]), holds=len(holds),
                     hold_max_ms=ms(max(holds or [0])),
                     hold_ms=[round(ms(c), 1) for c in holds],
                     reveal_ms=ms(hs[ifd]["cycles"] - hs[ixb]["cycles"]))
            res["fs_back" if lab == "title" else "fs_back_" + lab] = r
            print("  %s: the way back (%s): first light %d cycles = %.0f ms (mode set, the "
                  "desktop, the content black); then the picture in %d holds, the longest "
                  "%.1f ms, all out %.0f ms later" % (tag, lab, r["cycles"], r["ms"], r["holds"],
                                                    r["hold_max_ms"], r["reveal_ms"]),
                  flush=True)
            presented(ui, g)
            return r

        # --- F at the title ---------------------------------------------------------
        whole("title", lambda: m.key("KeyF"))
        res["fs_present"] = res["fs_present_title"]
        if tag != "ega":
            for seed, tier, lab, rounds in PERF_ROUNDS:
                hs_all = []
                want = comp_steps(seed, tier)
                for r in range(1, max(rounds) + 1):
                    if r == 1:
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
                        go = ("it_tpause", 0)
                    if r not in rounds:
                        g.put(*go)
                        M.until(m, lambda _: g.b("it_tpause") == 1, "round %d's end" % r,
                                poll=0.1, guest=120)
                        continue
                    with M.bp_trace(m, HS0, AI1, HS1, cap=12000) as tr:
                        g.put(*go)
                        tr.until(lambda: g.b("it_tpause") == 1, "round %d's end" % r,
                                 limit=300)
                    assert not tr.overflowed, "%s round %d: the trace overflowed" % (lab, r)
                    hs_r = [c for c, _ in pairs(tr.hits, HS0, HS1)]
                    assert len(hs_r) == want[r - 1], "%s round %d: %d head-steps traced, " \
                        "the model's round has %d" % (lab, r, len(hs_r), want[r - 1])
                    hs_all += hs_r
                    print("  %s full screen, %s round %d: %d head-steps, median %d"
                          % (tag, lab, r, len(hs_r), stats(hs_r)["med"]), flush=True)
                st = stats(hs_all)
                res["fs_headstep_" + lab] = st
                print("  %s full-screen head-step + span (%s): %s" % (tag, lab, st), flush=True)
                to_title(g)                 # the game ends; the title stays full
                fs_settled(g)               # screen (Esc in a game)
        # --- Esc at the title: THE WAY BACK (SPEC.md 53.6, 108.12.6) -----------------
        way_back("title", "Escape")
        # --- IN A ROUND: F's present with the HUD up, and F back -------------------
        start_game(g, 2425, 1, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and not g.b("it_vsync")
                and g.w("it_iter") > 100, "a round in play", poll=0.1, guest=60)
        m.pause()
        g.put("it_tpause", 1)           # held: the HUD alone, nothing moves
        m.run()
        presented(ui, g)
        whole("round", lambda: m.key("KeyF"))
        way_back("round", "KeyF")
        # --- EGA: a Lunatic h4 frame, the governor's ---------------------------------
        if tag == "ega":
            to_title(g)
            presented(ui, g)
            start_game(g, 702, 3, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
            M.until(m, lambda _: g.b("it_tpause") == 1, "round 1", poll=0.2, guest=120)
            m.pause()
            g.put("it_h", 4)
            g.put("it_tflags", L.TF_BOT | L.TF_NOFLOW)
            g.put("it_tpause", 0)
            m.run()
            M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and not g.b("it_vsync")
                    and g.b("it_hplayed") == 4, "a round at h4", poll=0.1, guest=30)
            n0 = g.w("it_fsn")
            m.key("KeyF")
            M.until(m, lambda _: g.b("it_fsm") == 1 and g.w("it_fsn") > n0, "full screen",
                    poll=0.1, guest=40)
            m.pause()
            g.put("it_pspeed", 9)
            m.run()
            FF0, FF1 = B + g.code["it_fsf0"], B + g.code["it_fs_frame"]
            ga, sa, ia = g.a("it_gskip"), g.a("it_state"), g.a("it_iter")

            def onhit(mm, rec):
                return (mm.read(ga, 1)[0], mm.read(sa, 1)[0],
                        struct.unpack("<H", bytes(mm.read(ia, 2)))[0])
            with M.bp_trace(m, FF0, FF1, cap=800, on_hit=onhit) as tr:
                tr.wait(2 * 300, limit=600)
            # THE BRACKET'S GOVERNOR (itfs.inc .e1) engages on a presenting
            # frame that runs into the NEXT tick - the tick and not pit_now,
            # FASTTICK runs PIT channel 0 at 3x - so a frame over one tick
            # (54.9 ms) with the governor never on after it is the failure
            fr, last = [], None
            over, engaged, seen = False, False, False
            for h in tr.hits:
                if h["addr"] == FF0:
                    last = h
                elif h["addr"] == FF1 and last is not None:
                    if last["hit"][1] == L.ST_PLAY and h["hit"][1] == L.ST_PLAY:
                        c = h["cycles"] - last["cycles"]
                        fr.append(c)
                        if c > M.GUEST_HZ / S.TICK_HZ:
                            over = True
                    if h["hit"][0]:
                        seen = True
                        if over:
                            engaged = True
                    last = None
            plays = [h for h in tr.hits if h["addr"] == FF1 and h["hit"][1] == L.ST_PLAY]
            span = plays[-1]["cycles"] - plays[0]["cycles"] if len(plays) > 1 else 0
            its = plays[-1]["hit"][2] - plays[0]["hit"][2] if len(plays) > 1 else 0
            st = stats(fr)
            st.update(ms_med=ms(st["med"]), ms_max=ms(st["max"]), over_tick=over,
                      engaged=engaged, governor_seen=seen,
                      iters_per_s=its / (span / M.GUEST_HZ) if span else 0)
            res["fs_frame_lunatic4"] = st
            print("  ega full screen, Lunatic h4 at speed 9: a frame's work %s = %.1f ms "
                  "median, %.1f ms most; governor engaged: %s; %.0f iterations a second"
                  % (st, st["ms_med"], st["ms_max"], "yes" if seen else "no",
                     st["iters_per_s"]), flush=True)
            # ...and its other half, which this machine's frames do not reach:
            # engaged by hand, 32 presents that end in their own tick let go
            m.pause()
            g.put("it_gskip", 1)
            g.put("it_gquiet", 0)
            m.run()
            M.until(m, lambda _: g.b("it_gskip") == 0, "the governor to let go", poll=0.1,
                    guest=15)
            print("  ega: the governor, engaged by hand, let go after %d quiet presents"
                  % 32, flush=True)
            m.pause()
            g.put("it_qesc", 1)
            m.run()
            M.until(m, lambda _: g.b("it_state") == L.ST_TITLE, "the title", guest=20)
            fs_settled(g)
            fs_leave(ui, g, "esc")
    bad = []
    if tag != "ega":
        for lab in ("rookie", "lunatic"):
            med = res["fs_headstep_" + lab]["med"]
            if med > FS_CEIL["step"][tag]:
                bad.append("%s full-screen head-step %d cycles > %d"
                           % (lab, med, FS_CEIL["step"][tag]))
        for lab in ("title", "round"):
            if res["fs_present_" + lab]["ms"] > FS_CEIL["present_ms"]:
                bad.append("F's whole present (%s) %.0f ms > %.0f"
                           % (lab, res["fs_present_" + lab]["ms"], FS_CEIL["present_ms"]))
    else:
        f = res["fs_frame_lunatic4"]
        if f["over_tick"] and not f["engaged"]:
            bad.append("EGA Lunatic h4: a frame over a tick and the governor never engaged")
    for k in ("fs_back", "fs_back_round"):
        if res[k]["ms"] > FS_CEIL["first_light_ms"]:
            bad.append("%s: first light %.0f ms > %.0f" % (k, res[k]["ms"],
                                                          FS_CEIL["first_light_ms"]))
        if res[k]["hold_max_ms"] > CEIL["band_hold"]:
            bad.append("%s: a band's hold %.1f ms > %.1f" % (k, res[k]["hold_max_ms"],
                                                            CEIL["band_hold"]))
    if out:
        import json
        json.dump(res, open(out, "w"), indent=1)
    assert not bad, "%s: %s" % (tag, "; ".join(bad))
    print("  %s: full screen under every section 16 ceiling it is gated on" % tag, flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rules", action="store_true", help="the isotronrules row")
    ap.add_argument("--all", action="store_true", help="all six stored games")
    ap.add_argument("--shot", metavar="DIR", help="keep a capture of the window")
    ap.add_argument("--bound", action="store_true", help="the isotronbound row")
    ap.add_argument("--win", metavar="TAG", help="cga, herc, vga, ega or all: isotronwin(ega)")
    ap.add_argument("--perf", metavar="TAG", help="cga, herc, vga, ega or all: isotronperf "
                    "(ega: the full-screen part alone)")
    ap.add_argument("--json", metavar="FILE", help="--perf: keep the numbers")
    ap.add_argument("--fs", metavar="TAG", help="cga, herc, vga, ega or all: isotronfs")
    ap.add_argument("--toggle", metavar="TAG", help="cga, vga or all: isotrontoggle")
    ap.add_argument("--twodisp", action="store_true", help="the isotrontwodisp row")
    ap.add_argument("--fsonly", action="store_true", help="--perf: the full-screen part alone")
    a = ap.parse_args()
    if a.rules:
        return rules(a)
    if a.bound:
        return bound_leg()
    if a.win:
        for tag in (("cga", "herc", "vga") if a.win == "all" else a.win.split(",")):
            win_leg(tag)
            if tag == "ega":            # isotronega is BOTH halves (SPEC.md 108.12)
                fs_leg("ega")
        print("isotronwin: ok (%s)" % a.win)
        return 0
    if a.fs:
        for tag in (("cga", "herc", "vga") if a.fs == "all" else a.fs.split(",")):
            fs_leg(tag)
        print("isotronfs: ok (%s)" % a.fs)
        return 0
    if a.toggle:
        for tag in (("cga", "vga") if a.toggle == "all" else a.toggle.split(",")):
            toggle_leg(tag)
        print("isotrontoggle: ok (%s)" % a.toggle)
        return 0
    if a.twodisp:
        twodisp_leg()
        print("isotrontwodisp: ok")
        return 0
    if a.perf:
        for tag in (("cga", "herc", "vga", "ega") if a.perf == "all" else a.perf.split(",")):
            if not a.fsonly and tag != "ega":   # (EGA's windowed lines are isotronega's)
                perf_leg(tag, a.json and a.json.replace(".json", "-%s.json" % tag))
            fs_perf_leg(tag, a.json and a.json.replace(".json", "-fs-%s.json" % tag))
        print("isotronperf: ok (%s)" % a.perf)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
