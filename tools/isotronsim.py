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
import os
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

    def __init__(self, h, tier, rng, grid=None, canvas=None, on_step=None):
        self.h, self.tier, self.cfg, self.rng = h, tier, TIERS[tier], rng
        self.grid = grid or Grid()
        self.grid.reset()
        # THE PICTURE (wave 2, SPEC.md 108.9): the slab and tower rebuilt for
        # this round, then every cell placed is one span down its column
        self.canvas = canvas
        self.on_step = on_step       # (round, idx, value, written) - selfcheck
        self.maxprobe = 0            # the bound counters (SPEC.md 108.10)
        self.maxspan = 0
        if canvas is not None:
            canvas.rebuild(h)
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
        if self.canvas is not None:
            nprobe, n, written = self.canvas.step(self.grid, i, v, self.h)
            self.maxprobe = max(self.maxprobe, nprobe)
            self.maxspan = max(self.maxspan, n)
            if self.on_step is not None:
                self.on_step(self, i, v, written)

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

    def __init__(self, seed, tier, canvas=False, on_step=None):
        self.canvas = Canvas() if canvas else None
        self.on_step = on_step
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
        rnd = Round(self.h, self.tier, self.rng, self.grid, self.canvas, self.on_step)
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
        if self.canvas is not None:
            rec["maxprobe"], rec["maxspan"] = rnd.maxprobe, rnd.maxspan
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


# --- the picture (SPEC.md 108.9 - 108.11, wave 2) ------------------------------
# ONE adapter-independent 320 x 200 picture of 4-bit SURFACE IDS. A nibble is
# a surface, not a colour: each presenter turns it into colour or pattern
# through its own look-up table, so a look change or a window/full-screen
# toggle is a PRESENT and never a redraw of the game.
#
# THE GUEST STORES IT ONE BYTE A PIXEL, COLUMN-MAJOR (pack()): pixel (x, y)
# is byte x * 200 + y, 64,000 bytes, an EVEN column's id in the byte's high
# nibble and an ODD column's in its low nibble - so the OR of a column pair's
# bytes is the plan's packed canvas byte (high nibble = even x), and a
# presenter's table takes it as it is. What it buys is the step: a wall is one
# column, consecutive bytes, one `rep stosb` (SPEC.md 108.10.3). The canvas is
# a claim of its own.
CW, CH = 320, 200
CCOL = CH                            # bytes a canvas column
CSIZE = CW * CH                      # 64,000

VOID, FLOOR, FLOORDOT, RIM, EDGE, SLABF, SLABR = 0, 1, 2, 3, 4, 5, 6
PFRONT, PSIDE, TOP, CFRONT, CSIDE = 7, 8, 9, 10, 11
LOGOF, FLASH = 13, 15
# TOP is EVERY wall's top pixel, whoever's: one id, because the guest finds
# k* as the first TOP byte below a new cell in its column (SPEC.md 108.10.1)
# and a scan compares one value. 12 and 14 are free.
IDNAMES = ("VOID", "FLOOR", "FLOORDOT", "RIM", "EDGE", "SLABF", "SLABR", "PFRONT",
           "PSIDE", "TOP", "CFRONT", "CSIDE", "free12", "LOGOF", "free14", "FLASH")

C_LOGO = 0x30                        # a title letter's cell (owner bits 11)
WALL_H = 23                          # a wall: 1 top pixel + 22 face rows
PROBES = 22                          # SPEC.md 108.10: at most 22 probes
DIAG = GW - 1                        # the front-diagonal step: (u+1, d-1) = -216
LEVEL_DY = 24                        # a tower level is 24 rows above the last
Y_FRONT = 174                        # the front outline's row at level 1

MUT.update(probe_limit=PROBES, unclipped=False)


def wall_ids(v):
    """(top id, face id) of a grid cell's value (owner | axis bit)."""
    owner, ns = v & 0x30, v & 1
    if owner == C_PLAYER:
        return TOP, (PSIDE if ns else PFRONT)
    if owner == C_COMP:
        return TOP, (CSIDE if ns else CFRONT)
    return TOP, LOGOF


def level_parts(j, full):
    """The runs of slab level j (1-based) as (y, x0, x1, id) in DRAW ORDER,
    x1 inclusive; FLOORDOT is resolved per pixel by the writer. A full level
    is the whole slab; a LOWER BODY is what shows of a level under the one
    above it: the front outline, the right outline, the front and right
    faces (SPEC.md 108.9.2)."""
    o = LEVEL_DY * (j - 1)
    out = []
    for t in range(74, 197):
        y = t - o
        if full:
            if t == 74:
                out.append((y, 100, 316, EDGE))
            if 74 < t <= 174:
                out.append((y, 174 - t, 174 - t, EDGE))
            if 75 <= t <= 96:
                out.append((y, 175 - t, 389 - t, RIM))
            if 97 <= t <= 173:
                out.append((y, 175 - t, 196 - t, RIM))
                out.append((y, 197 - t, 389 - t, FLOOR))      # dots by the writer
        if t == 174:
            out.append((y, 0, 216, EDGE))
        if 74 <= t <= 173:
            out.append((y, 390 - t, 390 - t, EDGE))
        if 175 <= t <= 196:
            out.append((y, 0, 215, SLABF))
        if 75 <= t <= 196:
            x0, x1 = max(217, 391 - t), min(316, 412 - t)
            if x0 <= x1:
                out.append((y, x0, x1, SLABR))
    return out


# THE TITLE'S LETTERING (SPEC.md 108.9.4): "isoTron" as strokes on the floor,
# each one a straight run of grid cells that the wall renderer extrudes - so
# the letters stand in the game's own projection. Our own block letters,
# typed here: (u0, d0, u1, d1), one axis constant, inclusive.
def _logo_strokes():
    xh, cap = 44, 64                 # x-height and cap height, in cells: tall
    u, d0 = 14, 18                   # enough that a back stroke's 22-row face
    w, tw, gap = 22, 30, 7           # stops short of the front one's top edge
    s = []

    def seg(a0, b0, a1, b1):
        s.append((u + a0, d0 + b0, u + a1, d0 + b1))
    # i: a stem and its dot
    seg(1, 0, 1, xh)
    seg(1, xh + 7, 1, xh + 11)
    u += 3 + gap
    # s
    seg(0, 0, w - 1, 0)
    seg(w - 1, 0, w - 1, xh // 2)
    seg(0, xh // 2, w - 1, xh // 2)
    seg(0, xh // 2, 0, xh)
    seg(0, xh, w - 1, xh)
    u += w + gap
    # o
    seg(0, 0, w - 1, 0)
    seg(0, 0, 0, xh)
    seg(w - 1, 0, w - 1, xh)
    seg(0, xh, w - 1, xh)
    u += w + gap
    # T
    seg(0, cap, tw - 1, cap)
    seg(tw // 2, 0, tw // 2, cap)
    u += tw + gap
    # r: a stem, its arm and the arm's drop
    seg(0, 0, 0, xh)
    seg(0, xh, 16, xh)
    seg(16, xh - 8, 16, xh)
    u += 17 + gap
    # o
    seg(0, 0, w - 1, 0)
    seg(0, 0, 0, xh)
    seg(w - 1, 0, w - 1, xh)
    seg(0, xh, w - 1, xh)
    u += w + gap
    # n
    seg(0, 0, 0, xh)
    seg(0, xh, w - 1, xh)
    seg(w - 1, 0, w - 1, xh)
    return s


LOGO = _logo_strokes()


def logo_cells():
    """Every cell of the lettering, in the guest's order: stroke by stroke,
    each from (u0, d0) to (u1, d1), a cell already set skipped."""
    out, seen = [], set()
    for u0, d0, u1, d1 in LOGO:
        assert u0 == u1 or d0 == d1, "a stroke is a straight run"
        ns = 1 if u0 == u1 else 0
        for d in range(d0, d1 + 1):
            for u in range(u0, u1 + 1):
                if (u, d) not in seen:
                    seen.add((u, d))
                    out.append((u, d, C_LOGO | ns))
    return out


class Canvas:
    """The 320 x 200 picture of surface ids (one id a pixel here; pack() is
    the guest's 64,000 bytes)."""

    def __init__(self):
        self.px = bytearray(CW * CH)
        self.ctop = [0] * CW         # the guest's it_ctop: a column's lowest TOP

    def copy(self):
        c = Canvas()
        c.px[:] = self.px
        c.ctop[:] = self.ctop
        return c

    def put_run(self, y, x0, x1, ident):
        if not (0 <= y < CH):
            raise AssertionError("a slab row outside the picture: %d" % y)
        row = y * CW
        if ident == FLOOR:
            for x in range(x0, x1 + 1):
                self.px[row + x] = FLOORDOT if (x + y) & 3 == 0 else FLOOR
        else:
            self.px[row + x0:row + x1 + 1] = bytes([ident]) * (x1 - x0 + 1)

    def rebuild(self, h):
        """canvas_rebuild(h): VOID, the lower bodies of levels 1..h-1, then
        level h whole. The one-row gap between levels stays VOID."""
        self.px[:] = bytes(CW * CH)
        self.ctop = [0] * CW
        for j in range(1, h + 1):
            for y, x0, x1, ident in level_parts(j, j == h):
                self.put_run(y, x0, x1, ident)

    @staticmethod
    def kstar(grid, i):
        """Lemma 2: the least k in 1..22 with (u+k, d-k) occupied, else 23.
        MUT['probe_limit'] is the negative control's 21."""
        g = grid.g
        for k in range(1, MUT["probe_limit"] + 1):
            if g[i - DIAG * k]:
                return k
        return WALL_H

    def step(self, grid, i, v, h):
        """The step span (SPEC.md 108.10): the new cell's column, its top
        pixel and min(k*, 23) - 1 face rows. Returns (probes, rows, written).

        PROBES is what the guest's k* scan EXAMINES (itcanvas.inc it_span,
        SPEC.md 108.10.2), modelled from the grid rather than read off a
        canvas: none on the fast path - no TOP below this one in its column
        yet (`ctop`) - and otherwise min(k*, len), len = min(22, k_ring - 1)
        bytes, the ring being the step's nearest outline cell down the front
        diagonal, which the canvas has no byte for."""
        u, d = i % GW, i // GW
        k = self.kstar(grid, i)
        n = WALL_H if MUT["unclipped"] else min(k, WALL_H)
        top, face = wall_ids(v)
        x, y = u + d, Y_FRONT - d - LEVEL_DY * (h - 1)
        if self.ctop[x] > y:
            ln = min(MUT["probe_limit"], min(GW - 1 - u, d) - 1)
            probes = max(0, min(k, ln))
        else:
            self.ctop[x] = y
            probes = 0
        written = []
        for r in range(n):
            p = (y + r) * CW + x
            self.px[p] = top if r == 0 else face
            written.append(p)
        return probes, n, written

    def title(self, grid):
        """The title picture: level 1 and the lettering, each letter cell a
        wall placed with the step span. The grid is the title's own."""
        grid.reset()
        self.rebuild(1)
        for u, d, v in logo_cells():
            i = idx(u, d)
            grid.g[i] = v
            self.step(grid, i, v, 1)

    # --- an INDEPENDENT full repaint: back to front, unclipped walls --------
    def column_painter(self, base, grid, x, h):
        """Column x of the picture for this grid, the painter's way: the slab
        column from `base`, then every wall in the column BACK TO FRONT, each
        23 rows, cut only at the ring's own row there. No probe and no k*, so
        it is a second derivation of Lemma 2 rather than a copy of it."""
        o = LEVEL_DY * (h - 1)
        col = [base.px[y * CW + x] for y in range(CH)]
        ring = min(Y_FRONT, 390 - x) - o            # the front or right ring
        g = grid.g
        for d in range(GH - 2, 0, -1):              # d = 99 .. 1, back first
            u = x - d
            if not (1 <= u <= GW - 2):
                continue
            v = g[d * GW + u]
            if v == 0 or v == C_EDGE:
                continue
            top, face = wall_ids(v)
            y = Y_FRONT - d - o
            for r in range(WALL_H):
                if y + r >= ring:
                    break
                col[y + r] = top if r == 0 else face
        return col

    def full_repaint(self, grid, h):
        base = Canvas()
        base.rebuild(h)
        out = base.copy()
        for x in range(CW):
            col = out.column_painter(base, grid, x, h)
            for y in range(CH):
                out.px[y * CW + x] = col[y]
        return out

    # --- the guest's bytes ----------------------------------------------------
    def pack(self):
        """64,000 bytes, column-major: byte x*200 + y is the id of (x, y),
        shifted to the high nibble in an even column."""
        out = bytearray(CSIZE)
        px = self.px
        for x in range(CW):
            sh = 4 if x & 1 == 0 else 0
            o = x * CCOL
            for y in range(CH):
                out[o + y] = px[y * CW + x] << sh
        return bytes(out)

    @staticmethod
    def unpack(raw):
        c = Canvas()
        for x in range(CW):
            o = x * CCOL
            for y in range(CH):
                v = raw[o + y]
                c.px[y * CW + x] = (v >> 4) if x & 1 == 0 else (v & 15)
        return c


# --- the looks (SPEC.md 108.11.1) ---------------------------------------------
# 1bpp: two device pixels a logical pixel, (left << 1 | right), for (device
# row parity, logical x parity). Ours, and they must be LOOKED at (§39.4).
def _m(r0, r1=None, by_x=False):
    if by_x:                         # (even x, odd x), the same on both rows
        return ((r0, r1), (r0, r1))
    r1 = r0 if r1 is None else r1
    return ((r0, r0), (r1, r1))


MONO = [
    _m(0b00),                        # VOID
    _m(0b00),                        # FLOOR
    _m(0b10, 0b00),                  # FLOORDOT
    _m(0b11, 0b00),                  # RIM: horizontal stripes
    _m(0b11),                        # EDGE
    _m(0b10, 0b01),                  # SLABF: checker
    _m(0b10, 0b00),                  # SLABR
    _m(0b11),                        # PFRONT
    _m(0b11, 0b10),                  # PSIDE
    _m(0b11),                        # TOP: every wall's top pixel
    _m(0b00, 0b11, by_x=True),       # CFRONT: vertical stripes
    _m(0b11, 0b00, by_x=True),       # CSIDE
    _m(0b00),                        # (12, free)
    _m(0b10, 0b00),                  # LOGOF: dim, so the edges read
    _m(0b00),                        # (14, free)
    _m(0b11),                        # FLASH
]

# 16 colours, the "isoTron" look: (even x, odd x), EGA/VGA default palette
COLOUR = [
    (0, 0), (0, 0), (1, 1), (5, 5), (15, 15), (8, 7), (0, 8), (11, 11),
    (3, 3), (15, 15), (5, 13), (13, 5), (0, 0), (1, 1), (0, 0), (12, 12),
]

EGA_RGB = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
           (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255),
           (85, 255, 85), (85, 255, 255), (255, 85, 85), (255, 85, 255),
           (255, 255, 85), (255, 255, 255)]

# --- the windowed geometry (SPEC.md 108.11.2) ---------------------------------
WIN_W = 2 * CW                       # 2x across: 640 device pixels, 80 bytes
WIN_HMAX = 400                       # Hc = min(content height, 400)
WIN_HMIN = 120


def row_map(hc):
    """s(r) = (r * 200) / Hc: the logical row device row r shows."""
    return [(r * CH) // hc for r in range(hc)]


def row_first(hc):
    """rfirst[y] = the first device row showing logical row y or later
    (ceil(y * Hc / 200)); [y0, y1) is device rows [rfirst[y0], rfirst[y1])."""
    return [(y * hc + CH - 1) // CH for y in range(CH + 1)]


def win1(canvas, hc, rows=None):
    """The windowed 1bpp picture, per pixel: hc rows of 640 bits. The row
    parity is the device row's within the picture."""
    s = row_map(hc)
    out = []
    for r in (range(hc) if rows is None else rows):
        y = s[r]
        par = r & 1
        line = bytearray(WIN_W)
        base = y * CW
        for x in range(CW):
            pat = MONO[canvas.px[base + x]][par][x & 1]
            line[2 * x] = (pat >> 1) & 1
            line[2 * x + 1] = pat & 1
        out.append(line)
    return out


def win4(canvas, hc, rows=None):
    """The windowed 16-colour picture, per pixel: hc rows of 640 indices."""
    s = row_map(hc)
    out = []
    for r in (range(hc) if rows is None else rows):
        y = s[r]
        line = bytearray(WIN_W)
        base = y * CW
        for x in range(CW):
            c = COLOUR[canvas.px[base + x]][x & 1]
            line[2 * x] = c
            line[2 * x + 1] = c
        out.append(line)
    return out


# --- the guest's tables, built the guest's way (itwin.inc it_lut_build) -------
def lut_mono():
    """[parity] -> (HI[256], LO[256]): a packed canvas byte (two logical px)
    to the device nibble it shows, in the high or the low half of a byte."""
    out = []
    for par in (0, 1):
        hi, lo = bytearray(256), bytearray(256)
        for b in range(256):
            e, o = b >> 4, b & 15
            nib = (MONO[e][par][0] << 2) | MONO[o][par][1]
            hi[b], lo[b] = nib << 4, nib
        out.append((hi, lo))
    return out


def lut_colour():
    """(TA, TB): 256 four-byte entries, plane p's byte of a packed canvas
    byte's four device pixels as the HIGH nibble (TA, the left canvas byte of
    a device byte) or the LOW nibble (TB)."""
    ta, tb = [], []
    for b in range(256):
        ce, co = COLOUR[b >> 4][0], COLOUR[b & 15][1]
        a, bb = [], []
        for p in range(4):
            nib = (((ce >> p) & 1) * 0b1100) | (((co >> p) & 1) * 0b0011)
            a.append(nib << 4)
            bb.append(nib)
        ta.append(a)
        tb.append(bb)
    return ta, tb


def _pair(raw, x, y):
    """The packed canvas byte of columns x, x + 1 (x even) at row y."""
    return raw[x * CCOL + y] | raw[(x + 1) * CCOL + y]


def win1_lut(raw, hc):
    """win1 through the guest's tables, from the guest's 64,000 bytes."""
    luts, s = lut_mono(), row_map(hc)
    out = []
    for r in range(hc):
        hi, lo = luts[r & 1]
        y = s[r]
        line = bytearray()
        for j in range(WIN_W // 8):
            line.append(hi[_pair(raw, 4 * j, y)] | lo[_pair(raw, 4 * j + 2, y)])
        out.append(bytes(line))
    return out


def win4_lut(raw, hc):
    """win4 through the guest's tables: per row, four planes of 80 bytes."""
    ta, tb = lut_colour()
    s = row_map(hc)
    out = []
    for r in range(hc):
        y = s[r]
        planes = [bytearray(), bytearray(), bytearray(), bytearray()]
        for j in range(WIN_W // 8):
            b0, b1 = _pair(raw, 4 * j, y), _pair(raw, 4 * j + 2, y)
            for p in range(4):
                planes[p].append(ta[b0][p] | tb[b1][p])
        out.append(planes)
    return out


def bits_of(rowbytes):
    return [(b >> (7 - k)) & 1 for b in rowbytes for k in range(8)]


def planes_to_index(planes):
    n = len(planes[0]) * 8
    out = bytearray(n)
    for p in range(4):
        for i, b in enumerate(planes[p]):
            for k in range(8):
                if (b >> (7 - k)) & 1:
                    out[i * 8 + k] |= 1 << p
    return out


# --- pictures, for people (standard library only) -----------------------------
def write_png(path, w, h, rgb_rows):
    import struct
    import zlib

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rgb_rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def png_win(path, canvas, hc, mono):
    rows = win1(canvas, hc) if mono else win4(canvas, hc)
    rgb = []
    for line in rows:
        if mono:
            rgb.append(b"".join(b"\xff\xff\xff" if v else b"\0\0\0" for v in line))
        else:
            rgb.append(b"".join(bytes(EGA_RGB[v]) for v in line))
    write_png(path, WIN_W, hc, rgb)


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
    # 5. the picture (wave 2): fixtures, step-by-step equality, the tables
    bad += canvas_selfcheck()
    # 6. one round played twice is the same round (no hidden state)
    a = Game(1234, 2).play_round()
    b = Game(1234, 2).play_round()
    if a != b:
        bad += _fail("the model is not deterministic")
    if bad:
        print("isotronsim: %d problem(s)" % bad)
        return 1
    print("isotronsim: selfcheck ok - %d games, pace table, rng, tower rule, the "
          "canvas and its tables, 5 negative controls refused" % len(STORED))
    return 0


# --- the canvas selfcheck (SPEC.md 108.10) ------------------------------------
CANVAS_ROUNDS = 30                   # rounds held to the full repaint, step by step

FIXTURES = [
    # (name, cell placed first, cell placed second, its k*, probes, rows):
    # the second cell's nearest occupied diagonal is exactly k* in front
    ("k*=1", (100, 50), (99, 51), 1, 1, 1),
    ("k*=22", (120, 40), (98, 62), 22, 22, 22),
    ("k*=23", (84, 36), (60, 60), 23, 22, 23),   # a TOP 24 rows down: the
                                                  # scan's 22 find none
]


class StepCheck:
    """The on_step hook: after EVERY cell placed, the incremental canvas must
    equal an independent full repaint. Two halves, both exact:

      * every byte the step wrote is in the new cell's own column, and
      * that column equals Canvas.column_painter's - the walls of the column
        drawn back to front, 23 rows each, cut only at the ring - which shares
        no code with the probe.

    A column the step did not touch cannot differ from the full repaint's,
    because a column's picture is a function of that column's cells alone and
    none of them changed; so the two halves are the whole canvas, step by
    step, at a cost the selfcheck can pay for thirty rounds. Each round's END
    is also compared whole (Canvas.full_repaint), which needs no argument."""

    def __init__(self):
        self.bases = {}
        self.steps = 0
        self.bad = []

    def base(self, h):
        if h not in self.bases:
            c = Canvas()
            c.rebuild(h)
            self.bases[h] = c
        return self.bases[h]

    def __call__(self, rnd, i, v, written):
        self.steps += 1
        if self.bad:
            return
        u, d = i % GW, i // GW
        x = u + d
        if any(p % CW != x for p in written):
            self.bad.append("step %d wrote outside column %d" % (self.steps, x))
            return
        want = rnd.canvas.column_painter(self.base(rnd.h), rnd.grid, x, rnd.h)
        if bytes(rnd.canvas.px[x::CW]) != bytes(want):
            y = next(k for k in range(CH) if rnd.canvas.px[k * CW + x] != want[k])
            self.bad.append("step %d (cell u%d d%d, h%d): column %d differs from the full "
                            "repaint at row %d (%s, want %s)"
                            % (self.steps, u, d, rnd.h, x, y,
                               IDNAMES[rnd.canvas.px[y * CW + x]], IDNAMES[want[y]]))


def canvas_rounds(nrounds=CANVAS_ROUNDS):
    """Play the stored games, in order, for nrounds rounds with the canvas and
    the step check on. Returns (problems, steps, rounds, maxprobe, maxspan)."""
    chk = StepCheck()
    rounds = maxp = maxs = 0
    probs = []
    for seed, tier, _, _ in STORED:
        g = Game(seed, tier, canvas=True, on_step=chk)
        while not g.over() and rounds < nrounds and not chk.bad:
            rec = g.play_round()
            rounds += 1
            maxp, maxs = max(maxp, rec["maxprobe"]), max(maxs, rec["maxspan"])
            full = g.canvas.full_repaint(g.grid, rec["h"])
            if full.px != g.canvas.px:
                probs.append("seed %d round %d: the canvas differs from a whole repaint at "
                             "the round's end" % (seed, len(g.rounds)))
        if rounds >= nrounds or chk.bad:
            break
    return probs + chk.bad, chk.steps, rounds, maxp, maxs


def run_fixtures():
    """The section 108.10 fixtures. Returns the problems, [] when all hold."""
    probs = []
    for name, first, second, k, nprobe, rows in FIXTURES:
        grid, c = Grid(), Canvas()
        c.rebuild(1)
        for cell in (first, second):
            if cell is None:
                continue
            i = idx(*cell)
            grid.g[i] = C_PLAYER
            got = c.step(grid, i, C_PLAYER, 1)
        if Canvas.kstar(grid, idx(*second)) != k:
            probs.append("%s: k* is %d" % (name, Canvas.kstar(grid, idx(*second))))
        if got[:2] != (nprobe, rows):
            probs.append("%s: %d probes and %d rows, want %d and %d"
                         % (name, got[0], got[1], nprobe, rows))
        if c.full_repaint(grid, 1).px != c.px:
            probs.append("%s: the canvas differs from the full repaint" % name)
    return probs


def present_check():
    """The guest's look-up tables, built the guest's way, against the
    per-pixel reference of each look, on the title and on a tower of four."""
    probs = []
    g, t = Grid(), Canvas()
    t.title(g)
    game = Game(2425, 1, canvas=True)
    while game.h != 4 or not game.rounds:
        game.play_round()
    game.play_round()                    # a round PLAYED at h = 4
    for name, c in (("title", t), ("h4", game.canvas)):
        raw = c.pack()
        if Canvas.unpack(raw).px != c.px:
            probs.append("%s: pack/unpack is not the identity" % name)
        for hc in (136, 284):            # CGA and Hercules windows
            ref = win1(c, hc)
            lut = win1_lut(raw, hc)
            if any(bytes(bits_of(lut[r])) != bytes(ref[r]) for r in range(hc)):
                probs.append("%s: the mono tables differ from the per-pixel look at Hc %d"
                             % (name, hc))
        for hc in (286, 400):            # EGA and VGA windows
            ref = win4(c, hc)
            lut = win4_lut(raw, hc)
            if any(bytes(planes_to_index(lut[r])) != bytes(ref[r]) for r in range(hc)):
                probs.append("%s: the colour tables differ from the per-pixel look at Hc %d"
                             % (name, hc))
    return probs


GUEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "apps", "isotron")


def _db_block(text, label):
    """The numbers of the `db` lines after `label:` until a blank line."""
    k = text.index("\n" + label + ":")
    out = []
    for line in text[k + len(label) + 2:].split("\n"):
        line = line.split(";")[0].strip()
        if not line.startswith("db "):
            if out:
                break
            continue
        out += [int(v.strip(), 0) for v in line[3:].split(",")]
    return out


def guest_tables():
    """The guest's look tables and lettering, read out of its source, against
    this model's: a look or a stroke typed twice cannot drift."""
    probs = []
    win = open(os.path.join(GUEST, "itwin.inc"), encoding="utf-8").read()
    can = open(os.path.join(GUEST, "itcanvas.inc"), encoding="utf-8").read()
    mono = _db_block(win, "it_mono")
    want = [(m[0][0] << 6) | (m[0][1] << 4) | (m[1][0] << 2) | m[1][1] for m in MONO]
    if mono != want:
        probs.append("itwin.inc it_mono %r is not MONO %r" % (mono, want))
    col = _db_block(win, "it_col")
    want = [(e << 4) | o for e, o in COLOUR]
    if col != want:
        probs.append("itwin.inc it_col %r is not COLOUR %r" % (col, want))
    logo = _db_block(can, "it_logo")
    want = [v for st in LOGO for v in st] + [0xFF]
    if logo != want:
        probs.append("itcanvas.inc it_logo is not LOGO")
    return probs


def canvas_selfcheck():
    bad = 0
    for p in run_fixtures():
        bad += _fail(p)
    probs, steps, rounds, maxp, maxs = canvas_rounds()
    for p in probs:
        bad += _fail(p)
    if rounds != CANVAS_ROUNDS:
        bad += _fail("only %d rounds were held to the repaint" % rounds)
    if maxp > PROBES or maxs > WALL_H:
        bad += _fail("a step made %d probes or wrote %d rows" % (maxp, maxs))
    print("  canvas: %d steps over %d rounds, each equal to the full repaint; "
          "most probes %d, most rows %d" % (steps, rounds, maxp, maxs))
    for p in present_check():
        bad += _fail(p)
    for p in guest_tables():
        bad += _fail(p)
    # THE NEGATIVE CONTROLS: a probe that stops at 21, and a span that is not
    # clipped. Each must break the fixtures AND the step-by-step equality.
    for name, val in (("probe_limit", 21), ("unclipped", True)):
        old = MUT[name]
        MUT[name] = val
        try:
            fx = run_fixtures()
            st = canvas_rounds(2)[0]
        finally:
            MUT[name] = old
        if not fx or not st:
            bad += _fail("negative control %s=%r was NOT refused (fixtures %d, steps %d)"
                         % (name, val, len(fx), len(st)))
        else:
            print("  negative control %-11s refused (%s; %s)" % (name, fx[0], st[0][:60]))
    return bad


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
