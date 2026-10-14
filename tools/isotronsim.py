#!/usr/bin/env python3
"""isotronsim.py - isoTron's host reference model (SPEC.md 108).

    python3 tools/isotronsim.py --selfcheck
    python3 tools/isotronsim.py --game SEED --tier T [--bot] [--inputs FILE] [--json]
    python3 tools/isotronsim.py --rates

Standard library only. Nothing here reads anything outside the tree: the
rules are the remake's own, pinned in SPEC.md 108 and written down a second
time here as code. THIS FILE IS NORMATIVE for the rules, the computer's
decisions and the pace (SPEC.md 108.3 - 108.6): the guest (apps/isotron/)
equals it iteration for iteration, and tests/isotron.py holds the two
together after every round of seeded games on MartyPC.

What it models (wave 1):

  Rng      the 16-bit LCG, s = s*25173 + 13849, r = s >> 8, ONE byte a
           computer head-move and one at an h = 1 round start
  Grid     217 x 101 bytes, idx = d*217 + u, the outline ring occupied
  Round    one round: the player first, then each live computer head in
           index order; the iteration's virtual cost
  Game     scores, losses in a row (three end the game) and the tower:
           a player win that FOLLOWS a player win raises it, 4 + 1 = 1
  ai_move  the computer's decision (SPEC.md 108.6) - normative
  bot      the harness's player (it_tflags BOT): one F10 at iteration 0,
           and a turn toward the larger side when the way ahead is <= 3
  Pace     the iteration model's cost and the rates it produces

The canvas, the occlusion span and every presenter arrive in wave 2.
"""
import argparse
import hashlib
import json
import sys

# --- the grid (SPEC.md 108.2) ------------------------------------------------
GW, GH = 217, 101                    # u = 0..216, d = 0..100; the ring is the
GSIZE = GW * GH                      # outline. 21,917 bytes
N, E, S, W = 0, 1, 2, 3              # headings; left = (h+3)&3, right = (h+1)&3
DELTA = {N: GW, E: 1, S: -GW, W: -1}
DUV = {N: (0, 1), E: (1, 0), S: (0, -1), W: (-1, 0)}
C_EDGE = 0x03                        # the outline ring
C_PLAYER = 0x10                      # owner bits | AXIS (bit 0: 1 = the move
C_COMP = 0x20                        # that entered the cell was N/S)
P_START = (108, 27)                  # heading N
C_START = (108, 90)                  # h heads from this one cell
OPEN_LEG = 32                        # the opening head turns S after 32 cells
HEADS4 = (W, E, S, N)                # h >= 2: the first h of these
P_NORMAL, P_MAX = 2, 9               # the player's speed, sixths of a cell
FIFO_N = 3                           # the turn FIFO's depth
RUN_CAP = 255
U_TURN_MIN = 16                      # a wander / U-turn wants more than this
BOT_LOOK = 3

# --- the tiers (SPEC.md 108.4.1, 108.6) --------------------------------------
# A + B*live heads + C*player cells, in u = 1/768 of a BIOS tick (71.52 us)
TIERS = {
    1: dict(name="Rookie",  A=36, B=178, C=62, look=1, icpt=64,  wander=6),
    2: dict(name="Veteran", A=27, B=134, C=47, look=3, icpt=96,  wander=4),
    3: dict(name="Lunatic", A=18, B=89,  C=31, look=6, icpt=128, wander=2),
}
U_PER_TICK = 768
TICK_HZ = 1193182.0 / 65536.0        # 18.2065
DEBT_CAP = 2 * U_PER_TICK            # the governor's ceiling (SPEC.md 108.4.2)

LCG_MUL, LCG_ADD = 25173, 13849

# keys in an input script: (iteration, key)
K_LEFT, K_RIGHT, K_BOOST, K_NORMAL = 1, 2, 3, 4
KEYNAMES = {"L": K_LEFT, "R": K_RIGHT, "F10": K_BOOST, "F9": K_NORMAL}

# --- mutations, for the negative controls only --------------------------------
MUT = dict(every_win=False, lcg_mul=LCG_MUL, comp_first=False)


def idx(u, d):
    return d * GW + u


def left(h):
    return (h + 3) & 3


def right(h):
    return (h + 1) & 3


def rol16(x):
    return ((x << 1) | (x >> 15)) & 0xFFFF


class Rng:
    """s = s*25173 + 13849 (mod 65536); the byte is s >> 8. One MUL."""

    def __init__(self, seed):
        self.s = seed & 0xFFFF

    def byte(self):
        self.s = (self.s * MUT["lcg_mul"] + LCG_ADD) & 0xFFFF
        return self.s >> 8


class Grid:
    def __init__(self):
        self.g = bytearray(GSIZE)
        self.reset()

    def reset(self):
        g = self.g
        g[:] = bytes(GSIZE)
        for u in range(GW):
            g[idx(u, 0)] = C_EDGE
            g[idx(u, GH - 1)] = C_EDGE
        for d in range(GH):
            g[idx(0, d)] = C_EDGE
            g[idx(GW - 1, d)] = C_EDGE

    def run(self, i, hd, cap):
        """Free cells from i heading hd, at most cap (SPEC.md 108.6)."""
        dl, g, n = DELTA[hd], self.g, 0
        while n < cap:
            i += dl
            if g[i]:
                break
            n += 1
        return n


class Head:
    def __init__(self, u, d, hd):
        self.u, self.d, self.hd = u, d, hd
        self.i = idx(u, d)
        self.alive = True
        self.leg = 0                 # cells moved since the last turn
        self.turned = False          # has it turned at all this round
        self.upend = 0               # a U-turn armed: moves until it fires
        self.uside = 0               # ...and which side: 0 left, 1 right


class Round:
    """One round at tower height h. The RNG is the game's, shared across
    rounds; everything else is the round's own."""

    def __init__(self, h, tier, rng, grid=None):
        self.h, self.tier, self.cfg, self.rng = h, tier, TIERS[tier], rng
        self.grid = grid or Grid()
        self.grid.reset()
        self.ck = 0
        self.nsteps = 0
        self.it = 0
        self.u_total = 0             # virtual time, in u
        self.winner = None
        self.p = Head(P_START[0], P_START[1], N)
        self.pspeed = P_NORMAL
        self.pacc = 0
        self.fifo = []
        self._place(self.p.i, C_PLAYER | 1)
        if h == 1:
            r = rng.byte()
            hds = [W if (r & 1) else E]
        else:
            hds = list(HEADS4[:h])
        self.heads = [Head(C_START[0], C_START[1], hd) for hd in hds]
        self._place(self.heads[0].i, C_COMP | ((hds[0] & 1) ^ 1))
        # the opening head: index 0 at h = 1, the E head (index 1) above
        self.opener = 0 if h == 1 else 1

    def _place(self, i, v):
        self.grid.g[i] = v
        self.ck = rol16(self.ck) ^ i

    def _move(self, head, owner):
        """One cell. False = the head crashed and is dead."""
        ni = head.i + DELTA[head.hd]
        if self.grid.g[ni]:
            head.alive = False
            return False
        self._place(ni, owner | ((head.hd & 1) ^ 1))
        head.i = ni
        du, dd = DUV[head.hd]
        head.u += du
        head.d += dd
        head.leg += 1
        self.nsteps += 1
        return True

    def push_turn(self, k):
        if len(self.fifo) < FIFO_N:
            self.fifo.append(k)

    def iterate(self, nboost=0, f9=False):
        """One iteration (SPEC.md 108.3). Returns the winner or None."""
        cfg = self.cfg
        live0 = sum(1 for c in self.heads if c.alive)
        if f9:
            self.pspeed = P_NORMAL
        self.pspeed = min(P_MAX, self.pspeed + nboost)
        pm = 0
        result = None
        if MUT["comp_first"]:
            self._computer()
        self.pacc += self.pspeed
        while self.pacc >= 6:
            self.pacc -= 6
            if self.fifo:
                k = self.fifo.pop(0)
                self.p.hd = left(self.p.hd) if k == K_LEFT else right(self.p.hd)
                self.p.leg = 0
            pm += 1
            if not self._move(self.p, C_PLAYER):
                result = "C"
                break
        if result is None and not MUT["comp_first"]:
            self._computer()
        if result is None and not any(c.alive for c in self.heads):
            result = "P"
        self.u_total += cfg["A"] + cfg["B"] * live0 + cfg["C"] * pm
        self.it += 1
        self.winner = result
        return result

    def _computer(self):
        for n, c in enumerate(self.heads):
            if not c.alive:
                continue
            r = self.rng.byte()
            ai_move(self, c, n, r)
            self._move(c, C_COMP)


def ai_move(rnd, c, n, r):
    """The computer's decision for head c (index n), r its RNG byte.
    NORMATIVE (SPEC.md 108.6): the first rule that turns ends it."""
    cfg, g, p = rnd.cfg, rnd.grid, rnd.p
    look = cfg["look"]

    def turn(hd):
        c.hd, c.leg, c.turned = hd, 0, True

    # 1. the opening
    if (n == rnd.opener and not c.turned and c.hd in (E, W)
            and c.leg == OPEN_LEG and g.run(c.i, S, look + 1) > look):
        return turn(S)
    # 2. blocked
    if g.run(c.i, c.hd, look + 1) <= look:
        lr = g.run(c.i, left(c.hd), RUN_CAP)
        rr = g.run(c.i, right(c.hd), RUN_CAP)
        if lr > rr:
            return turn(left(c.hd))
        if rr > lr:
            return turn(right(c.hd))
        if lr == 0:
            return None                          # straight on, and crash
        return turn(left(c.hd) if (r & 1) == 0 else right(c.hd))
    # 3. a U-turn pending
    if c.upend:
        c.upend -= 1
        if c.upend == 0:
            side = left(c.hd) if c.uside == 0 else right(c.hd)
            if g.run(c.i, side, U_TURN_MIN + 1) > U_TURN_MIN:
                return turn(side)
    # 4. a short leg goes straight
    if c.leg < 8:
        return None
    # 5. intercept
    if r < cfg["icpt"] and p.alive:
        pdu, pdd = DUV[p.hd]
        for side in (left(c.hd), right(c.hd)):
            if (side & 1) == (p.hd & 1):
                continue                         # parallel to the player
            du, dd = DUV[side]
            if du:
                t = (p.u - c.u) * du
                k = (c.d - p.d) * pdd
            else:
                t = (p.d - c.d) * dd
                k = (c.u - p.u) * pdu
            if t <= 0 or k <= 0:
                continue
            pi = (k * 6 + rnd.pspeed - 1) // rnd.pspeed
            if t + 2 < pi <= 120 and g.run(c.i, side, t) >= t:
                return turn(side)
    # 6. wander, and arm the U-turn
    if (r ^ 0xA5) < cfg["wander"]:
        lft = r & 1
        side = left(c.hd) if lft else right(c.hd)
        if g.run(c.i, side, U_TURN_MIN + 1) > U_TURN_MIN:
            turn(side)
            c.upend = 8 + (r & 7)
            c.uside = 0 if lft else 1
            return None
    return None


def bot_input(rnd):
    """The harness's player (it_tflags BOT, SPEC.md 108.7.2): its keys for the
    coming iteration, as (nboost, f9). Pushes at most one turn, and only into
    an empty FIFO, so a turn is never queued twice for one wall."""
    nboost = 1 if rnd.it == 0 else 0
    p = rnd.p
    if p.alive and not rnd.fifo and rnd.grid.run(p.i, p.hd, BOT_LOOK + 1) <= BOT_LOOK:
        lr = rnd.grid.run(p.i, left(p.hd), RUN_CAP)
        rr = rnd.grid.run(p.i, right(p.hd), RUN_CAP)
        rnd.push_turn(K_LEFT if lr >= rr else K_RIGHT)
    return nboost, False


def script_input(rnd, script):
    """An (iteration, key) script (it_tinput): its keys for this iteration."""
    nboost, f9 = 0, False
    for it, k in script:
        if it != rnd.it:
            continue
        if k in (K_LEFT, K_RIGHT):
            rnd.push_turn(k)
        elif k == K_BOOST:
            nboost += 1
        elif k == K_NORMAL:
            f9 = True
    return nboost, f9


ROUND_LIMIT = 30000                  # no round can outlast the grid's cells


class Game:
    """Score, three losses in a row, and the tower."""

    def __init__(self, seed, tier):
        self.rng = Rng(seed)
        self.tier = tier
        self.h = 1
        self.score = [0, 0]
        self.lossrow = 0
        self.prev = None
        self.rounds = []
        self.grid = Grid()
        self.nsteps = 0

    def over(self):
        return self.lossrow >= 3

    def play_round(self, bot=True, script=()):
        hplayed = self.h
        rnd = Round(self.h, self.tier, self.rng, self.grid)
        while True:
            if bot:
                nb, f9 = bot_input(rnd)
            else:
                nb, f9 = script_input(rnd, script)
            w = rnd.iterate(nb, f9)
            if w:
                break
            if rnd.it > ROUND_LIMIT:
                raise RuntimeError("a round outlasted the grid")
        if w == "P":
            self.score[0] += 1
            self.lossrow = 0
            if self.prev == "P" or MUT["every_win"]:
                self.h += 1
                if self.h == 5:
                    self.h = 1
        else:
            self.score[1] += 1
            self.lossrow += 1
        self.prev = w
        self.nsteps += rnd.nsteps
        rec = dict(h=hplayed, winner=w, iter=rnd.it, sp=self.score[0], sc=self.score[1],
                   hnext=self.h, lossrow=self.lossrow, rng=self.rng.s, ck=rnd.ck,
                   nsteps=rnd.nsteps, u=rnd.u_total)
        self.rounds.append(rec)
        return rec

    def play(self, bot=True, script=(), max_rounds=200):
        """Rounds until three losses in a row. A script plays in EVERY round,
        at the iterations it names (the guest's it_tinput does the same)."""
        while not self.over() and len(self.rounds) < max_rounds:
            self.play_round(bot, script)
        return self.rounds


def game_hash(rounds):
    keys = ("h", "winner", "iter", "sp", "sc", "hnext", "lossrow", "rng", "ck")
    blob = json.dumps([[r[k] for k in keys] for r in rounds]).encode()
    return hashlib.sha1(blob).hexdigest()[:16]


def towers(rounds):
    """The height each round was played at, in order."""
    return [r["h"] for r in rounds]


def has_tower_cycle(rounds):
    """1, 2, 3, 4 and then 1 again, in that order, somewhere in the game."""
    hs = towers(rounds)
    want, k = (1, 2, 3, 4, 1), 0
    for h in hs:
        if h == want[k]:
            k += 1
            if k == len(want):
                return True
    return False


# --- pace (SPEC.md 108.4) ----------------------------------------------------
class Pace:
    """The iteration model's rates, at steady state."""

    @staticmethod
    def rates(tier, h, p):
        cfg = TIERS[tier]
        cost_u = cfg["A"] + cfg["B"] * h + cfg["C"] * (p / 6.0)
        secs = cost_u / U_PER_TICK / TICK_HZ
        its = 1.0 / secs
        return its, its * p / 6.0, its * h + its * p / 6.0   # head, player, head-steps

    @staticmethod
    def table():
        out = []
        for tier in (1, 2, 3):
            for h in (1, 2, 3, 4):
                for p in (P_NORMAL, P_MAX):
                    c, pl, hs = Pace.rates(tier, h, p)
                    out.append((tier, h, p, round(c, 1), round(pl, 1), round(hs)))
        return out


# --- the selfcheck ------------------------------------------------------------
# Six seeded bot games, two a tier, each covering a tower 1-2-3-4-1 and ending
# in three losses in a row. The hash is game_hash() over every round's record.
STORED = [
    # (seed, tier, rounds, hash) - found with --find, checked by --selfcheck.
    # The FIRST game of each tier is the one tests/isotron.py replays on the
    # guest (the shortest that covers both), the second a spare.
    (2425, 1, 8, "70d791e8d9eafe62"),
    (1588, 1, 9, "97cbd986579ef091"),
    (981, 2, 11, "fbd3273e9cc79444"),
    (26, 2, 11, "1a5c28e37aada2ea"),
    (702, 3, 9, "55a8d9329d335c1b"),
    (410, 3, 10, "6de6bf815e63ccc5"),
]

# A scripted (no bot) game the guest replays through it_tinput: F10 twice at
# the start, a right turn, a left, F9 - every round, until three losses.
SCRIPT = [(0, K_BOOST), (0, K_BOOST), (20, K_RIGHT), (45, K_LEFT), (70, K_NORMAL)]
SCRIPT_GAME = (4242, 2)

FIXED_RATES = [  # (tier, h, p, head cells/s, player cells/s): SPEC.md 108.4.1
    (1, 1, 2, 59.6, 19.9), (1, 2, 2, 33.9, 11.3), (1, 3, 2, 23.7, 7.9),
    (1, 4, 2, 18.2, 6.1), (1, 1, 9, 45.5, 68.3), (1, 2, 9, 28.8, 43.2),
    (1, 3, 9, 21.1, 31.6), (1, 4, 9, 16.6, 24.9),
]


def _fail(msg):
    print("FAIL: " + msg)
    return 1


def run_stored():
    out = []
    for seed, tier, nr, hs in STORED:
        g = Game(seed, tier)
        rounds = g.play()
        out.append((seed, tier, len(rounds), game_hash(rounds), rounds))
    return out


def selfcheck():
    bad = 0
    # 1. the pace table
    for tier, h, p, hc, pc in FIXED_RATES:
        c, pl, _ = Pace.rates(tier, h, p)
        if abs(c - hc) > 0.15 or abs(pl - pc) > 0.15:
            bad += _fail("rates tier %d h%d p%d: %.1f/%.1f, want %.1f/%.1f"
                         % (tier, h, p, c, pl, hc, pc))
    worst = max(Pace.rates(3, 1, 9)[2], 0)
    if round(worst) != 228:
        bad += _fail("Lunatic's worst head-steps/s is %.1f, want 228" % worst)
    # 2. the rng's first bytes from seed 0
    rr = Rng(0)
    seq = [rr.byte() for _ in range(4)]
    if seq != [54, 190, 121, 67]:
        bad += _fail("rng sequence from seed 0 is %r, want [54, 190, 121, 67]" % seq)
    # 3. the stored games
    if len(STORED) != 6:
        bad += _fail("STORED has %d games, want 6 (run --find)" % len(STORED))
    tiers_seen = set()
    for seed, tier, nr, hs, rounds in run_stored():
        want = [x for x in STORED if x[0] == seed and x[1] == tier][0]
        tiers_seen.add(tier)
        if (nr, hs) != (want[2], want[3]):
            bad += _fail("game seed %d tier %d: %d rounds %s, stored %d %s"
                         % (seed, tier, nr, hs, want[2], want[3]))
        if not has_tower_cycle(rounds):
            bad += _fail("game seed %d tier %d has no tower 1-2-3-4-1" % (seed, tier))
        last = rounds[-1]
        if last["lossrow"] != 3 or [r["winner"] for r in rounds[-3:]] != ["C"] * 3:
            bad += _fail("game seed %d tier %d does not end in three losses" % (seed, tier))
        if any(r["iter"] <= 0 for r in rounds):
            bad += _fail("an empty round")
        # the tower rule, re-derived from the winners alone
        h, prev = 1, None
        for r in rounds:
            if r["h"] != h:
                bad += _fail("seed %d: round played at h%d, the rule says h%d" % (seed, r["h"], h))
                break
            if r["winner"] == "P" and prev == "P":
                h = 1 if h == 4 else h + 1
            prev = r["winner"]
    if tiers_seen != {1, 2, 3}:
        bad += _fail("the stored games cover tiers %r" % sorted(tiers_seen))
    # 4. the negative controls: each mutation must change at least one hash
    for name, val in (("every_win", True), ("lcg_mul", 25175), ("comp_first", True)):
        old = MUT[name]
        MUT[name] = val
        try:
            changed = [s for s, t, nr, hs, _ in run_stored()
                       if (nr, hs) != [(x[2], x[3]) for x in STORED if x[0] == s and x[1] == t][0]]
        finally:
            MUT[name] = old
        if not changed:
            bad += _fail("negative control %s=%r was NOT refused: every stored hash still "
                         "matches" % (name, val))
        else:
            print("  negative control %-10s refused (%d of %d games differ)"
                  % (name, len(changed), len(STORED)))
    # 5. one round played twice is the same round (no hidden state)
    a = Game(1234, 2).play_round()
    b = Game(1234, 2).play_round()
    if a != b:
        bad += _fail("the model is not deterministic")
    if bad:
        print("isotronsim: %d problem(s)" % bad)
        return 1
    print("isotronsim: selfcheck ok - %d games, pace table, rng, tower rule, "
          "3 negative controls refused" % len(STORED))
    return 0


def _rng_first(seed, n):
    s, out = seed, []
    for _ in range(n):
        s = (s * LCG_MUL + LCG_ADD) & 0xFFFF
        out.append(s >> 8)
    return out


def find(tier, start, count, need=2):
    """Search seeds whose bot game covers a tower 1-2-3-4-1 (authoring aid)."""
    got = []
    for seed in range(start, start + count):
        g = Game(seed, tier)
        rounds = g.play()
        if has_tower_cycle(rounds) and g.over():
            got.append((seed, tier, len(rounds), game_hash(rounds), sum(r["nsteps"] for r in rounds)))
            if len(got) >= need:
                break
    return got


def load_script(path):
    out = []
    for line in open(path):
        line = line.split("#")[0].strip()
        if not line:
            continue
        it, key = line.split()
        out.append((int(it), KEYNAMES[key.upper()]))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--game", type=int, metavar="SEED")
    ap.add_argument("--tier", type=int, default=1, choices=(1, 2, 3))
    ap.add_argument("--bot", action="store_true")
    ap.add_argument("--inputs", metavar="FILE")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--rates", action="store_true")
    ap.add_argument("--find", type=int, nargs=2, metavar=("START", "COUNT"))
    a = ap.parse_args(argv)
    if a.selfcheck:
        return selfcheck()
    if a.rates:
        print("tier   h  p   head/s  player/s  head-steps/s")
        for t, h, p, c, pl, hs in Pace.table():
            print("%-7s %d  %d  %6.1f  %8.1f  %6d" % (TIERS[t]["name"], h, p, c, pl, hs))
        return 0
    if a.find:
        for row in find(a.tier, a.find[0], a.find[1]):
            print(row)
        return 0
    if a.game is not None:
        g = Game(a.game, a.tier)
        script = load_script(a.inputs) if a.inputs else ()
        rounds = g.play(bot=a.bot or not script, script=script)
        if a.json:
            print(json.dumps(rounds))
        else:
            for r in rounds:
                print("h%(h)d %(winner)s it=%(iter)5d %(sp)d-%(sc)d next h%(hnext)d "
                      "loss %(lossrow)d rng %(rng)04x ck %(ck)04x" % r)
            print("hash", game_hash(rounds))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
