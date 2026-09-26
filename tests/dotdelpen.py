#!/usr/bin/env python3
"""DOT DELIRIUM's PEN, and the pellets that share its bug class (SPEC.md 93).

Three questions, all three of them field reports:

  A  EVERY PELLET IS REFRESHED, not just the first.  `dd_pills_blit` walked
     the pellet list with SI as its counter and `dd_tile_put` loaded SI with
     the band's address, so the first pellet trampled the loop.  The other
     three did not merely stop blinking: the first actor to cross one
     composed it in whichever phase was current, and if that was the dark
     half the pellet was gone for the rest of the board.  The report was
     *"the dot in the upper left corner is blinking, the rest can be eaten
     but not seen"*, and the upper left is pellet 0.
  B  A PENNED GHOST WANDERS THE PEN (SPEC.md 93.8.6).  The interior is six
     tiles by three; a ghost that bobs one up and one down reads as a machine
     waiting.  This asks for a real share of the box and more than one column.
  C  A GHOST THAT GOT HOME AS EYES STAYS THERE for DD_PENWAIT = 55 ticks
     before it comes back out, wandering while it waits.
  E  THE TUNNEL WRAPS, BOTH WAYS (SPEC.md 93.7.4).  A position is unsigned
     and dd_advance decides it has crossed into the previous tile by comparing
     against that tile's origin - which at column 0 is ZERO, so the step past
     it borrowed and read as a very large x rather than as a crossing.  Smiles
     walked off the left of the world and never stood on a tile origin again.
  F  AN EATEN PELLET STAYS EATEN (SPEC.md 93.5.7.1).  dd_pills_flip walks the
     pellet LIST, which is never pruned, so the blink lettered an eaten pellet
     straight back in 91 ms after the eater's own band had put black over it.
  H  A DOT IS A BITE (SPEC.md 93.10.1).  One note per dot at 660 Hz, four and
     a half times a second for a board of two hundred and forty, is the
     clink the field called grating.  Two tones a tick apart, with the pair
     turned over on the next dot, is the arcade's up-down-up-down.  It
     listens to the ATTRACT DEMO - the only place Smiles eats and cannot be
     caught - and reads the SHAPE and not the frequencies, which are a
     listening decision and will be retuned.
  G  THE DEATH IS AN ANIMATION (SPEC.md 93.5.16).  `dd_die` set a state and a
     timer and nothing else, so being caught was a thirty-two-tick freeze
     with four ghosts standing on Smiles.  This forces a real catch - a
     ghost written onto his own position, so `dd_collide` fires on the next
     tick and the REAL `dd_die` runs - and walks `dd_dietab` a tick at a
     time off `dd_die_anim`'s own exit.
  D  NOTHING IS DRAWN THROUGH A RUNNING SCREEN SAVER (SPEC.md 79.6.1).  This
     one is a KERNEL gate driven through this package because a package is
     the only thing that can reach it: a saver session is not a window, so
     nothing put a background painter off the screen and every real-time
     package in the tree drew straight through one.  The fix is `wm_clip_set`
     refusing while `[blk_sv]` is set, so what this reads is that `dd_draw`
     is never entered while the saver owns the glass.

BREAK IT ON PURPOSE: take the two instructions out of `wm_clip_set` and leg D
goes red at once.  Take `dd_die`'s `.hide` loop out and leg G names the ticks a
ghost was still on the board; put `dd_die_anim`'s call back AFTER the `dec` and
it reads the table one entry short at both ends.  Point `.dot` back at
`dd_beep` and leg H sees one tone a bite; take the `xor byte [dd_wakph], 1` out
and it sees every bite the same way round.  Take the `jc` out of `dd_advance`'s left arm and leg E's
first half reads columns 0 and 1 and nothing else, for ever.  Take
`dd_pills_flip`'s grid test out and leg F sees the blink fill a tile the grid
calls empty.  Make `dd_pills_flip` walk its list in SI and call
`dd_tile_put`, the way it used to, and leg A goes red - the run that proved
this saw `dd_tile_put` reached for {(1,3), (1,23), (13,17)} where the fixed
build reaches every pellet.  Put `dd_gh_house` back on its up/down bob and
leg B reads three tiles in one column.  Send `dd_gh_home`'s `.arrived`
straight to GS_OUT and leg C reads a wait of 0.

WHAT THIS ROW DOES NOT READ is the pellet's colour or its shape - those are a
look, and leg B of tests/dotdel.py is where the blink's PIXELS are checked.
Leg A here is about which pellets the refresh reaches, which is a fact and
not a picture: it is read off SI at a breakpoint inside `dd_pills_flip`'s own
loop, so a walk that stops early is caught wherever it stops.

One adapter is enough for all three: none of them is about the surface.  It
runs on the Hercules 5150 because that is the machine the reports came from.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88build                                            # noqa: E402
import os88sym                                              # noqa: E402
from dotdel import PKG, bss, Probe, codeoff                 # noqa: E402
import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402

MACHINE = "os8088_5150_herc_gla"
GS = {0: "HOUSE", 1: "OUT", 2: "ROAM", 3: "FRIGHT", 4: "EYES"}
GS_HOUSE, GS_ROAM, GS_EYES = 0, 2, 4
DDS_PLAY = 2

PENWAIT = 55                    # DD_PENWAIT, ticks
WAIT_LO, WAIT_HI = 45, 70       # ...and the window a sampled reading may land
                                # in: the sampler cannot see the tick it
                                # arrived on, so it always reads a little low
PEN_TILES = 5                   # of the pen's eighteen
PEN_COLS = 2                    # ...spread over at least this many columns




def leg_a(ui, p, say):
    """Every pellet on the board gets refreshed, not only pellet 0."""
    m = ui.m
    m.pause()
    npill = p.w("dd_piln")
    where = [(p.b("dd_pilc", i), p.b("dd_pilr", i)) for i in range(npill)]
    m.go()
    if npill < 2:
        say("A  FAIL: the board has %d pellet(s) - nothing to walk" % npill)
        return 1
    # SI at the top of dd_pills_flip's loop is the pellet it is about to
    # consider, so a walk that stops early is caught wherever it stops.
    off = codeoff("dd_pills_flip.each")
    m.breakpoints([{"type": "execseg", "seg": p.seg, "off": off}])
    want = set(range(npill))
    seen = set()
    for _ in range(12 * npill):
        m.go()
        if m.wait_stop(8.0) is None:
            break
        seen.add(m.regs()["si"])
        if want <= seen:
            break
    m.breakpoints([])
    m.go()
    miss = want - seen
    if miss:
        say("A  FAIL: %d of %d pellet(s) never refreshed: %s  (the walk "
            "reached indices %s)"
            % (len(miss), npill, sorted(where[i] for i in miss), sorted(seen)))
        return 1
    say("A  ok: all %d pellets refreshed %s" % (npill, sorted(where)))
    return 0


def guest_window(m, secs):
    """Go round for what `secs` of an IDLE box bought - counted on the GUEST's
    clock at os88marty.pace's rate, so a loaded box hands every leg the same
    amount of game to sample and not a third of it. The host sleeps between
    samples stay: they only set how often a leg looks. A clock that stops
    moving raises, as every os88marty wait does, rather than looping on."""
    budget = secs * (os88marty.GUEST_PACE or 4.5)
    c0 = last = m.status()["cycles"]
    moved = time.time()
    while True:
        c = m.status()["cycles"]
        if c != last:
            last, moved = c, time.time()
        elif time.time() - moved > os88marty.GUEST_STALL:
            raise os88marty.MartyError("the guest clock stopped at cycle %d "
                                       "in a %gs sampling window" % (c, secs))
        if (c - c0) / os88marty.GUEST_HZ >= budget:
            return
        yield


def leg_b(ui, p, say, secs=12.0):
    """A ghost sitting in the pen moves round it."""
    m = ui.m
    tiles = {}
    for _ in guest_window(m, secs):
        m.pause()
        for g in range(4):
            if p.b("dd_gs", g) == GS_HOUSE:
                tiles.setdefault(g, set()).add(
                    (p.b("dd_ac", g + 1), p.b("dd_ar", g + 1)))
        m.go()
        time.sleep(0.08)
    if not tiles:
        say("B  FAIL: no ghost was in the pen at all over %.0fs" % secs)
        return 1
    best = max(len(v) for v in tiles.values())
    bestc = max(len({c for c, _ in v}) for v in tiles.values())
    for g, ts in sorted(tiles.items()):
        say("     ghost %d: %d tile(s), %d column(s)"
            % (g, len(ts), len({c for c, _ in ts})))
    if best < PEN_TILES or bestc < PEN_COLS:
        say("B  FAIL: the busiest penned ghost saw %d tile(s) in %d column(s) "
            "- want %d in %d. A bob is 3 tiles in 1"
            % (best, bestc, PEN_TILES, PEN_COLS))
        return 1
    say("B  ok: %d of the pen's 18 tiles, over %d columns" % (best, bestc))
    return 0


def leg_c(ui, p, say, want=3, tries=10):
    """Eyes get home and serve DD_PENWAIT there before coming back out."""
    m = ui.m
    base = (p.seg << 4) + p.names["dd_gs"]
    lives = (p.seg << 4) + p.names["dd_lives"]
    got = 0
    fail = 0
    while got < want and tries > 0:
        tries -= 1
        g = None
        for _ in guest_window(m, 16.0):
            m.pause()
            for i in range(4):
                if p.b("dd_gs", i) == GS_ROAM:
                    g = i
                    break
            m.go()
            if g is not None:
                break
            time.sleep(0.2)
        if g is None:
            say("C  FAIL: no ghost was ever roaming, so none could be eaten")
            return 1
        m.pause()
        m.write(lives, bytes([99]))         # an unsteered Smiles dies often,
        m.write(base + g, bytes([GS_EYES]))  # and a death resets every ghost
        m.go()
        tin = None
        pen = set()
        verdict = None
        for _ in guest_window(m, 40.0):
            m.pause()
            st = p.b("dd_gs", g)
            tick = p.w("dd_anim")
            state = p.b("dd_state")
            ac, ar = p.b("dd_ac", g + 1), p.b("dd_ar", g + 1)
            m.go()
            if state != DDS_PLAY:
                verdict = ("skip",)
                break
            if st == GS_HOUSE:
                if tin is None:
                    tin = tick
                pen.add((ac, ar))
            elif tin is not None:
                verdict = ("done", tick - tin, len(pen), GS.get(st, "?"))
                break
            time.sleep(0.05)
        if verdict is None:
            say("C  FAIL: ghost %d never came back out of the pen" % g)
            return 1
        if verdict[0] == "skip":
            continue                        # a death: this leg is about the pen
        _, waited, ntiles, now = verdict
        ok = WAIT_LO <= waited <= WAIT_HI
        say("     ghost %d waited %d tick(s) (want ~%d), %d tile(s), then %s%s"
            % (g, waited, PENWAIT, ntiles, now, "" if ok else "   <-- WRONG"))
        if not ok:
            fail = 1
        got += 1
    if got == 0:
        say("C  FAIL: every trial was cut short by a death")
        return 1
    if fail:
        say("C  FAIL: a wait fell outside %d..%d ticks" % (WAIT_LO, WAIT_HI))
        return 1
    say("C  ok: %d ghost(s) home, penned for ~%d ticks, then out" % (got, PENWAIT))
    return 0


def leg_d(ui, p, say, ticks=40):
    """A saver session owns the glass, so the game draws nothing (79.6.1)."""
    m = ui.m
    idle = os88sym.linear("ss_idle")
    sv = os88sym.linear("blk_sv")
    m.pause()
    was = bytes(m.read(idle, 2))
    m.write(idle, ticks.to_bytes(2, "little"))   # os88ui.boot turns it off
    m.go()
    up = 0
    for _ in guest_window(m, 40.0):
        m.pause()
        up = m.read(sv, 1)[0]
        m.go()
        if up:
            break
        time.sleep(0.25)
    if not up:
        m.pause()
        m.write(idle, was)
        m.go()
        say("D  FAIL: the saver never started in 40s, so nothing was tested")
        return 1
    off = codeoff("dd_draw")
    m.breakpoints([{"type": "execseg", "seg": p.seg, "off": off}])
    m.go()
    hit = m.wait_stop(10.0)
    m.breakpoints([])
    m.go()
    os88marty.pace(m, 0.3)
    m.pause()
    still = m.read(sv, 1)[0]
    m.write(idle, was)
    m.go()
    if hit is not None:
        say("D  FAIL: dd_draw ran while a saver session owned the glass - the "
            "game is drawing over it (SPEC.md 79.6.1)")
        return 1
    if not still:
        say("D  FAIL: the saver stopped during the watch, so the quiet proves "
            "nothing")
        return 1
    say("D  ok: nothing drawn while the saver ran")
    return 0


TT_EMPTY, TT_PILL = 1, 3


def no_hazard(m, p):
    """Make the four of them EYES, which cannot catch anybody.

    dd_collide skips GS_EYES outright, so this takes the hazard out of a leg
    that is not about the hazard - and it has to be re-applied as the leg
    samples, because a pair of eyes gets home in a couple of seconds and comes
    back out as a ghost. Retrying a trial that a death cut short is not enough
    on its own: placing Smiles beside the pen and walking him into it dies
    four times out of four.
    """
    base = (p.seg << 4) + p.names["dd_gs"]
    for i in range(4):
        m.write(base + i, bytes([GS_EYES]))


def settle_play(m, p, secs=6.0):
    """Wait until the game is actually PLAYING, and keep Smiles in lives.

    Nothing moves outside DDS_PLAY - dd_step decrements a timer in READY and
    in DIE and never calls dd_act_move - so a trial that starts there proves
    nothing about whatever it was testing.
    """
    for _ in guest_window(m, secs):
        m.pause()
        st = p.b("dd_state")
        m.write((p.seg << 4) + p.names["dd_lives"], bytes([99]))
        m.go()
        if st == DDS_PLAY:
            return True
        time.sleep(0.2)
    return False


def place(m, p, c, r):
    """Put Smiles on the origin of tile (c, r) - all four position words."""
    seg = p.seg
    tw, th = p.w("dd_tw"), p.w("dd_th")
    m.pause()
    m.write((seg << 4) + p.names["dd_ac"], bytes([c]))
    m.write((seg << 4) + p.names["dd_ar"], bytes([r]))
    for n in ("dd_x", "dd_acx"):
        m.write((seg << 4) + p.names[n], (c * tw * 16).to_bytes(2, "little"))
    for n in ("dd_y", "dd_ary"):
        m.write((seg << 4) + p.names[n], (r * th * 16).to_bytes(2, "little"))
    m.go()


DIR_R, DIR_L = 0, 2


def steer(m, p, d):
    """Point Smiles at d, by the STATE and not by the keyboard.

    This leg is about SPEC.md 93.7.4 - whether the tunnel wraps - and driving
    it through dd_input made it a test of the input path as well, which is
    what it kept failing on: the right-hand trial follows the left-hand one,
    and a run where the ArrowRight never displaced the held ArrowLeft walked
    him LEFT for the whole seven seconds and reported the tunnel.  The columns
    said so plainly - 25 down to 18 under a key that means right - and it went
    one run in two.  dd_want is what dd_input would have set.
    """
    base = p.seg << 4
    m.pause()
    m.write(base + p.names["dd_dir"], bytes([d]))
    m.write(base + p.names["dd_want"], bytes([d]))
    m.go()


def leg_e(ui, p, say, secs=7.0, tries=4):
    """The tunnel row wraps in both directions."""
    m = ui.m
    fail = 0
    for tag, start, dirn, want in (("left", 2, DIR_L, lambda c: c >= 22),
                                   ("right", 25, DIR_R, lambda c: c <= 5)):
        ok = False
        for _ in range(tries):
            if not settle_play(m, p):
                continue
            place(m, p, start, 14)
            steer(m, p, dirn)
            cols = set()
            reset = False
            for _ in guest_window(m, secs):
                m.pause()
                c, r, st = p.b("dd_ac", 0), p.b("dd_ar", 0), p.b("dd_state")
                no_hazard(m, p)
                m.go()
                # A DEATH PUTS HIM BACK ON THE START TILE, which is a
                # different ROW - and a leg that watched only the column read
                # that as "walked left and never came out", naming the tunnel
                # for a trial that had stopped being about the tunnel.
                if r != 14 or st != DDS_PLAY:
                    reset = True
                    break
                cols.add(c)
                steer(m, p, dirn)       # ...and he is STILL pointed that way:
                time.sleep(0.06)        # dd_decide turns a ghost at a wall and
                                        # Smiles stops, so a single poke would
                                        # be a trial that ends at the first
                                        # junction rather than at the tunnel
            if reset:
                continue
            if any(want(c) for c in cols):
                say("     %-5s wrapped: columns %s" % (tag, sorted(cols)))
                ok = True
                break
            say("E  FAIL: walking %s off the tunnel row reached columns %s and "
                "never came out the other side (SPEC.md 93.7.4)"
                % (tag, sorted(cols)))
            fail = 1
            break
        if not ok and not fail:
            say("E  FAIL: every %s trial was cut short by a death, with the "
                "ghosts poked to EYES throughout - so something else is "
                "resetting him" % tag)
            fail = 1
    if not fail:
        say("E  ok: the tunnel wraps both ways")
    return fail


def leg_f(ui, p, say, tries=4):
    """A pellet that has been eaten is not lettered back in by the blink."""
    m = ui.m
    m.pause()
    pc, pr = p.b("dd_pilc", 0), p.b("dd_pilr", 0)
    tw, th = p.w("dd_tw"), p.w("dd_th")
    m.go()
    tile = TT_PILL
    for _ in range(tries):
        if not settle_play(m, p):       # nothing MOVES outside DDS_PLAY, so
            continue                    # a trial that starts in READY or DIE
        place(m, p, pc, pr)             # eats nothing and blames the eater
        for _ in guest_window(m, 1.5):
            m.pause()
            no_hazard(m, p)
            m.go()
            time.sleep(0.1)
        m.pause()
        tile = m.read((p.seg << 4) + p.names["dd_grid"] + pr * 28 + pc, 1)[0]
        st = p.b("dd_state")
        m.go()
        if tile != TT_PILL:
            break
        if st == DDS_PLAY:
            break                       # it really did not eat it
    if tile == TT_PILL:
        say("F  FAIL: standing on pellet 0 at (%d,%d) did not eat it" % (pc, pr))
        return 1
    off = codeoff("dd_fill_board")
    m.breakpoints([{"type": "execseg", "seg": p.seg, "off": off}])
    seen = 0
    bad = False
    for _ in range(8):
        m.go()
        if m.wait_stop(8.0) is None:
            break
        r = m.regs()
        seen += 1
        if r["ax"] // tw == pc and r["bx"] // th == pr:
            bad = True
            break
    m.breakpoints([])
    m.go()
    if bad:
        say("F  FAIL: the blink filled pellet 0's tile (%d,%d) after it was "
            "eaten (SPEC.md 93.5.7.1)" % (pc, pr))
        return 1
    if seen == 0:
        say("F  FAIL: the blink never ran, so nothing was tested")
        return 1
    say("F  ok: pellet 0 eaten, and %d blink fills later none is on its tile"
        % seen)
    return 0


DD_DIET = 18                     # ...and its table, which is the claim
DIE_WANT = ([9] * 3 + [10] * 2 + [11] * 2 + [37] * 2 + [38] * 2 + [39] * 2
            + [40] * 2 + [-1] * 3)


def leg_g(ui, p, say):
    """The death is an ANIMATION, and the ghosts leave for it."""
    m = ui.m
    names = p.names
    base = p.seg << 4
    # FORCE the catch rather than wait for one: a ghost put on Smiles' own
    # 1/16-px position is inside dd_collide's half-tile box on the very next
    # tick, so this runs the real dd_die and not a poked state.
    m.pause()
    if p.b("dd_state") != 2:                    # DDS_PLAY
        m.go()
        say("G  FAIL: not in play (state %d) - nothing to be caught during"
            % p.b("dd_state"))
        return 1
    m.write(base + names["dd_gs"], bytes([2]))  # GS_ROAM: dd_collide skips a
    px = bytes(m.read(base + names["dd_x"], 2))  # ghost that is EYES or in the
    py = bytes(m.read(base + names["dd_y"], 2))  # house, and eats a FRIGHT one
    m.write(base + names["dd_x"] + 2, px)        # dd_x/dd_y are word arrays by
    m.write(base + names["dd_y"] + 2, py)        # actor: +2 is ghost 0
    off = codeoff("dd_die_anim.out")            # written, and before the dec,
    m.breakpoints([{"type": "execseg", "seg": p.seg, "off": off}])
    m.go()
    walk = []
    for _ in range(DD_DIET + 4):
        if m.wait_stop(10.0) is None:
            break
        walk.append((p.w("dd_tim"), p.b("dd_img"), p.b("dd_alive"),
                     any(p.b("dd_alive", i) for i in range(1, 5))))
        m.go()
    m.breakpoints([])
    m.go()
    if not walk:
        say("G  FAIL: dd_die_anim was never reached - the death does not "
            "animate at all (SPEC.md 93.5.16)")
        return 1
    # dd_die calls it once itself and .die once a tick, so tim 18 comes up
    # twice; the picture is what the TABLE says for DD_DIET - tim either way.
    got, prev = [], None
    for tim, img, alive, _ in walk:
        if tim == prev:
            continue
        prev = tim
        got.append(img if alive else -1)
    got = got[:DD_DIET]
    if got != DIE_WANT:
        say("G  FAIL: the death walked %s\n   where SPEC.md 93.5.16's table "
            "is %s" % (got, DIE_WANT))
        return 1
    ghosts = [t for t, _, _, gh in walk if gh]
    if ghosts:
        say("G  FAIL: a ghost was still on the board at tick(s) %s of the "
            "death - dd_die must clear [dd_alive + 1..4] (SPEC.md 93.5.16)"
            % ghosts)
        return 1
    say("G  ok: %d ticks, images %s, no ghost on the board"
        % (len(got), got))
    return 0


def leg_h(ui, p, say, want=4):
    """A dot is a BITE - two tones - and the pair turns over on the next one.

    THE FREQUENCIES ARE NOT THE CLAIM.  DD_WAKLO and DD_WAKHI are a look-and-
    listen decision and will be retuned; what must not come back is the single
    note, so this reads the SHAPE: two tones a bite, different from each other,
    reversed bite to bite.
    """
    m = ui.m
    # THE DEMO, not a game.  dd_collide's demo arm skips dd_die, so the attract
    # AI drives Smiles round the board eating without being caught; a poked
    # [dd_want] in a real game walks him into a wall and then into a ghost, and
    # the only tone that comes back is dd_die's.
    for _ in guest_window(m, 20.0):
        m.pause()
        eaten, demo = p.w("dd_eaten"), p.b("dd_demo")
        m.go()
        if demo and eaten > 2:
            break
        time.sleep(0.25)
    else:
        say("H  FAIL: the demo never got going - nothing was eaten to listen to")
        return 1
    # AX at dd_tone's entry IS the frequency asked for and CX the ticks, so a
    # bite's two syllables are the CX=1 pair either side of [dd_eaten] moving.
    m.breakpoints([{"type": "execseg", "seg": p.seg,
                    "off": codeoff("dd_tone")}])
    seq = []
    for _ in range(6 * want + 8):
        m.go()
        if m.wait_stop(15.0) is None:
            break
        r = m.regs()
        seq.append((r["ax"] & 0xFFFF, r["cx"] & 0xFFFF, p.w("dd_eaten")))
    m.breakpoints([])
    m.go()
    bites, cur, last = [], [], None
    for hz, cx, e in seq:
        if cx != 1:
            continue                    # a pill, a ghost or a fruit note
        if e != last:
            if cur:
                bites.append((last, cur))
            cur, last = [], e
        cur.append(hz)
    if cur:
        bites.append((last, cur))
    pairs = [(e, b) for e, b in bites if len(b) == 2]
    if len(pairs) < want:
        say("H  FAIL: %d complete bite(s) of %d wanted in %d tone(s) - a dot "
            "is not two tones (SPEC.md 93.10.1).  bites=%s"
            % (len(pairs), want, len(seq), bites))
        return 1
    flat = [b for _, b in pairs if b[0] == b[1]]
    if flat:
        say("H  FAIL: %d bite(s) played the SAME tone twice (%s) - that is the "
            "clink again, not a warble (SPEC.md 93.10.1)" % (len(flat), flat))
        return 1
    # ...AND ONLY BETWEEN ADJACENT DOTS.  [dd_eaten] counts pellets too, and a
    # PELLET does not flip [dd_wakph] - it is dd_beep, not dd_dot_snd - so the
    # two dots either side of one share an orientation and always will.  That
    # is the design and not a defect, so the reversal is required where the
    # count moved by exactly one.
    stuck = [i for i in range(len(pairs) - 1)
             if pairs[i + 1][0] == pairs[i][0] + 1
             and pairs[i][1] != pairs[i + 1][1][::-1]]
    if stuck:
        say("H  FAIL: bite(s) %s did not turn the pair over - [dd_wakph] is "
            "not flipping, so every bite is the same syllable and the "
            "up-down-up-down is gone (SPEC.md 93.10.1).  bites=%s"
            % (stuck, [b for _, b in pairs]))
        return 1
    say("H  ok: %d bites, two tones each, reversed between adjacent dots "
        "(%s Hz)" % (len(pairs), sorted(set(pairs[0][1]))))
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", default=os88build.at("build/os8088-360.img"))
    ap.add_argument("--apps", default=os88build.at("build/apps360.img"))
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)

    out = []

    def say(s):
        out.append(s)
        if a.verbose:
            print(s, flush=True)

    names = bss()
    fail = 0
    with os88ui.boot(a.img, apps=a.apps, machine=a.machine) as ui:
        ui.path(PKG)
        p = Probe(ui, names)
        fail += leg_h(ui, p, say)      # the DEMO is what it listens to, so it
        ui.m.key("Enter")              # goes before the game starts
        # Until READY is over, as tests/dotdel.py's leg C waits - this was a
        # blind 13.5 guest seconds. The legs below say so if it never is.
        try:
            os88marty.until(ui.m, lambda _: p.b("dd_state") not in (0, 1),
                            "READY to end", poll=0.1, limit=15)
        except os88marty.MartyError:
            pass
        ui.m.pause()
        ui.m.write((p.seg << 4) + names["dd_lives"], bytes([99]))
        ui.m.go()
        fail += leg_a(ui, p, say)
        fail += leg_b(ui, p, say)
        fail += leg_c(ui, p, say)
        fail += leg_e(ui, p, say)
        fail += leg_f(ui, p, say)
        fail += leg_g(ui, p, say)   # ...and it KILLS Smiles, so                    it goes last of the game legs
        fail += leg_d(ui, p, say)      # last: it turns the screen saver ON

    if not a.verbose:
        for s in out:
            print(s)
    print("dotdelpen: %s" % ("ok" if not fail else "%d leg(s) FAILED" % fail))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
