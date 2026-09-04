#!/usr/bin/env python3
"""TANK ATTACK's aim assist and its reticle (SPEC.md 85.6.5) do what they claim.

    python3 tests/tankaim.py [--machine os8088_5150_cga_gla]

The defect this answers is arithmetic, so it asserts arithmetic. A heading is a
byte, `TK_TURN` spends two units of it a TICK, and `tk_input` latches once a
FRAME while `tk_pmove` spends up to `TK_MAXSTEP` - so the finest turn a player
can COMMAND on a 1bpp adapter is six units, and `tk_espoil`'s own window is 4.1
units wide at 3,000 and 3.1 at the shell's longest reach. The sweep steps over
a distant tank and the phase of the press decides whether it was ever hittable.

FOUR CLAIMS, and the first is a proof rather than a measurement.

**No bearing is unreachable.** The assist only helps inside the box the closed
sight draws, so the box has to be at least half the coarsest lattice the player
can be on or there are tanks nothing can aim at - which at the 18 px it was
first drawn at was one bearing in nine. Checked by ENUMERATION rather than by
SPEC.md 85.6.5.8's algebra, so a slip in the algebra shows up here.

**The turn itself is untouched**, which is what makes the rest of the game the
game it was: TK_TURN a tick, and no fraction anywhere in the heading.

**The gun helps inside the box and nowhere else.** A tank is placed at a KNOWN
bearing and the shell's heading is read out of the slot it spawned into: inside
the box it must leave corrected by `tk_aimfix`'s own cap, and outside it must
leave at `tk_pa` exactly - the player aims that one themselves.

**The reticle does not lie** (85.6.5.7). Rather than assume a geometry, this
reads the code's OWN inputs back - `tk_aimq`, the measured error, and
`tk_aimz`, the range - and asserts `tk_locked` against the window those imply
AFTER the correction the gun is about to make. A ladder of bearings then has to
produce both answers, or the check is vacuous.
"""
import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import tank as tanktest                                     # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, "apps", "tank", "tank.asm")

OT_FREE, OT_TANK, OT_SHELL = 0, 4, 5
TK_NSTAT, TK_NOBJ = 12, 18
TK_TURN = 2
MAXSTEP = 3                             # apps/tank's TK_MAXSTEP

RANGE = 3000                            # where the six-unit quantum stops
                                        # fitting inside the window at all
RETICLE = (1.0, 2.75, 4.25)             # the bearings the reticle ladder walks


def const(name):
    """A tank.asm equate, read out of the source so this cannot drift."""
    for line in open(ASM, encoding="utf-8"):
        m = re.match(r"\s*%s\s+equ\s+(\w+)" % re.escape(name), line)
        if m:
            return int(m.group(1), 0)
    sys.exit("tankaim: tank.asm has no %s" % name)


def reachable(boxq, turn, maxstep, grid=4096):
    """Is there a bearing no sequence of presses can put inside the box?

    THE QUESTION THIS ANSWERS IS THE ONE THAT WAS ASKED - "are there turns that
    could skip it?" - and it is answered by enumeration rather than by the
    algebra in SPEC.md 85.6.5.8, so that a slip in the algebra shows up here.

    The player's heading moves TK_TURN * tk_lstep units a frame and tk_lstep is
    capped at TK_MAXSTEP, so the reachable headings from wherever they start
    are a lattice of Q = TK_MAXSTEP * TK_TURN. A tank's bearing is anywhere;
    what matters is the WORST distance from a bearing to the nearest lattice
    point, and whether the box admits it."""
    q = turn * maxstep                          # the coarsest lattice, units
    worst, at = 0.0, 0.0
    for i in range(grid):                       # a bearing anywhere in one
        f = q * i / float(grid)                 # lattice cell, finely walked
        d = min(f, q - f)                       # ...to the nearer end of it
        if d > worst:
            worst, at = d, f
    return worst, at, q


def sintab():
    """tksin.inc, for placing a tank at a bearing the guest will agree with."""
    out = []
    for line in open(os.path.join(ROOT, "apps", "tank", "tksin.inc"),
                     encoding="utf-8"):
        line = line.strip()
        if line.startswith("dw"):
            out += [int(x) for x in line[2:].split(",")]
    if len(out) != 256:
        sys.exit("tankaim: tksin.inc is %d entries, not 256" % len(out))
    return out


class Game:
    """The package's own state, by name."""

    def __init__(self, m, seg, base):
        self.m, self.seg, self.base = m, seg, base

    def off(self, name):
        return self.base + dispapps.bss_off("tank", name)

    def rd(self, name, n=2, i=0):
        return self.m.readseg(self.seg, self.off(name) + i, n)

    def b(self, name, i=0):
        return self.rd(name, 1, i)[0]

    def w(self, name, i=0):
        return int.from_bytes(self.rd(name, 2, i), "little")

    def sw(self, name, i=0):
        v = self.w(name, i)
        return v - 0x10000 if v & 0x8000 else v

    def wr(self, name, data, i=0):
        self.m.write((self.seg << 4) + self.off(name) + i, bytes(data))

    def movers(self):
        t = self.rd("tk_otype", TK_NOBJ)
        return {i: t[i] for i in range(TK_NSTAT, TK_NOBJ)}

    def clear_movers(self):
        for i in range(TK_NSTAT, TK_NOBJ):
            self.wr("tk_otype", [OT_FREE], i)

    def quiet(self):
        """An empty, frozen world: tk_spawn = 0 shuts tk_update's spawner off
        at its own gate, so the only tank in play is the one placed here."""
        self.wr("tk_spawn", [0])
        self.wr("tk_dead", [0])
        self.clear_movers()

    def cap(self):
        """tk_aimfix's cap, in quarter units, for the frame rate in play."""
        return TK_TURN * 2 * self.b("tk_lstep")

    def place(self, slot, err, rng, sin):
        """A tank `err` units off the sights at `rng`.

        `err` may be fractional, which the guest's own 256-entry table cannot
        express - so a whole number is placed through that table, to the byte,
        and anything else through the same angle in floating point. The guest
        derives the bearing from the POSITION either way."""
        if err == int(err):
            a = (self.b("tk_pa") + int(err)) & 0xFF
            dx, dz = sin[a] / 16384.0, sin[(a + 64) & 0xFF] / 16384.0
        else:
            r = (self.b("tk_pa") + err) * math.pi / 128.0
            dx, dz = math.sin(r), math.cos(r)
        x = (self.w("tk_px") + int(rng * dx)) & 0x1FFF
        z = (self.w("tk_pz") + int(rng * dz)) & 0x1FFF
        self.wr("tk_ox", x.to_bytes(2, "little"), slot * 2)
        self.wr("tk_oz", z.to_bytes(2, "little"), slot * 2)
        self.wr("tk_oa", [0], slot)
        self.wr("tk_ocool", [255], slot)        # ...and it may not shoot back
        self.wr("tk_otim", [0], slot)
        self.wr("tk_otype", [OT_TANK], slot)

    def shell(self):
        """The heading of the player's live shell, if there is one."""
        t = self.rd("tk_otype", TK_NOBJ)
        for i in range(TK_NSTAT, TK_NOBJ):
            if t[i] == OT_SHELL:
                return i, self.b("tk_oa", i)
        return None, None


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    RETQ, HITQ, HYS = const("TK_RETQ"), const("TK_HITQ"), const("TK_LOCKHYS")
    BOXPX = const("TK_BOXPX")
    BOXQ = MAXSTEP * TK_TURN * 2 + 1        # tank.asm's TK_BOXQ, derived the
                                            # same way it is there

    def fix(err, cap, boxq):
        """tk_aimfix, in Python: the correction the gun applies."""
        if abs(err) > boxq:
            return 0                            # outside the box the player
        return max(-cap, min(cap, err))         # aims it themselves

    # --- 0. the arithmetic SPEC.md 85.6.5.8 rests on, before any of it runs ---
    worst, at, q = reachable(BOXQ, TK_TURN, MAXSTEP)
    print("  reachability: lattice %d units, worst bearing is %.3f units off "
          "the nearest reachable heading (at %+.3f); box is %.2f units "
          "(%d quarters, drawn at %d px)"
          % (q, worst, at, BOXQ / 4.0, BOXQ, BOXPX))
    if worst > BOXQ / 4.0:
        bad.append("a bearing %.3f units off the nearest commandable heading "
                   "cannot be put inside a box of %.2f units: there ARE turns "
                   "that skip it, and %.1f%% of bearings are unreachable "
                   "(SPEC.md 85.6.5.8)"
                   % (worst, BOXQ / 4.0, 100.0 * (worst - BOXQ / 4.0) / worst))
    SIN = sintab()
    bad = []

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = tanktest.open_game(m)
        g = Game(m, seg, base)
        print("  TANK.O88: window %d, segment %04x, bss at %04x"
              % (slot, seg, base))
        m.type_text("f")                        # into the bracket
        m.advance(frames=150)
        if g.w("tk_back") & 0xFF == 0:
            sys.exit("tankaim: the bracket refused its mode - nothing to test")
        print("  backend %d, viewport %dx%d, steps a frame %d"
              % (g.b("tk_back"), g.w("tk_vw"), g.w("tk_vh"), g.b("tk_lstep")))

        # --- 1. AIM OFF is the control arm, and is unchanged -----------------
        f0, t0, h0 = g.w("tk_frames"), g.w("tk_last"), g.b("tk_pa")
        m.key("KeyD", down=True, up=False)
        m.advance(frames=300)
        f1, t1, h1 = g.w("tk_frames"), g.w("tk_last"), g.b("tk_pa")
        m.key("KeyD", down=False, up=True)
        m.advance(frames=30)
        df, dt = f1 - f0, (t1 - t0) & 0xFFFF
        da = (h1 - h0) & 0xFF
        print("  the turn: held D over %d frames / %d ticks: heading +%d "
              "units, %.2f a tick, %.2f a frame"
              % (df, dt, da, da / max(1, dt), da / max(1, df)))
        if dt < 20 or da == 0:
            bad.append("the turn sample is degenerate (%d ticks, %d units): "
                       "nothing was measured" % (dt, da))
        elif not (TK_TURN * 0.6 <= da / dt <= TK_TURN * 1.15):
            bad.append("the player turned %.2f units a tick against TK_TURN = "
                       "%d: steering is paced by something other than the "
                       "clock, and the assist may not touch it "
                       "(SPEC.md 85.6.4)" % (da / dt, TK_TURN))

        # --- a fresh, quiet, FROZEN world for the three fixtures -------------
        # Section 1 spends the better part of a minute of guest time with a
        # tank hunting, so by here the player is usually dead and sometimes out
        # of lives - and both refuse: tk_fire tests tk_dead and tk_over, and
        # tk_input zeroes the latch under either. A fixture built on a finished
        # game measures nothing and reports it as "no shell".
        print("  state before the fixtures: over=%d dead=%d lives=%d"
              % (g.b("tk_over"), g.b("tk_dead"), g.b("tk_lives")))
        m.type_text("n")
        m.advance(frames=90)
        g.quiet()
        g.wr("tk_pause", [1])           # POKED, never toggled: a keystroke that
        m.advance(frames=40)            # misses leaves an arm running the wrong
        if g.b("tk_over") or g.b("tk_dead") or not g.b("tk_pause"):
            bad.append("no live frozen round to build the fixtures on "
                       "(over=%d dead=%d pause=%d)"
                       % (g.b("tk_over"), g.b("tk_dead"), g.b("tk_pause")))

        # --- 3. the gun helps INSIDE THE BOX and nowhere else -----------------
        # SPEC.md 85.6.5.8: the window is the closed sight's own box, so the
        # two bearings below are the whole test - one inside it and one out.
        lstep = g.b("tk_lstep")
        print("  box %d quarters, drawn at %d px; tk_lstep is %d here and the"
              " cap moves with it - each row below prints its own"
              % (BOXQ, BOXPX, lstep))
        for e in (3.0, 4.5):
            for _ in (0,):
                tag = "IN " if e * 4 <= BOXQ else "OUT"
                g.quiet()
                g.place(TK_NSTAT, e, RANGE, SIN)
                m.advance(frames=20)            # ...so tk_lockon measures it
                pa, err, cap = g.b("tk_pa"), g.sw("tk_aimq"), g.cap()
                m.type_text(" ")
                m.advance(frames=40)
                islot, sa = g.shell()
                corr = fix(err, cap, BOXQ)
                want = (pa + int(math.floor((corr + 2) / 4.0))) & 0xFF
                print("  %-4s: tank %+.1f units (%d quarters, %s the %d-quarter"
                      " box), cap %d; shell left at %s (want %d, a %d-quarter "
                      "fix)"
                      % (tag, e, err, "IN" if abs(err) <= BOXQ else "outside",
                         BOXQ, cap, sa, want, corr))
                if sa is None:
                    bad.append("%s at %+.1f: no shell spawned - tk_fire "
                               "refused and the assist was never reached"
                               % (tag, e))
                elif sa != want:
                    bad.append("%s at %+.1f units: the shell left at %d "
                               "against %d. An error of %d quarters %s the "
                               "%d-quarter box, with a cap of %d, must be "
                               "corrected by %d (SPEC.md 85.6.5.8)"
                               % (tag, e, sa, want, err,
                                  "inside" if abs(err) <= BOXQ else "outside",
                                  BOXQ, cap, corr))
                g.clear_movers()                # ...and the gun is free again

        # --- 4. the reticle does not lie -------------------------------------
        # Read the code's OWN inputs back rather than assuming a geometry:
        # tk_aimq is the error it measured and tk_aimz the range it measured
        # it at, so the window those imply is what tk_locked has to agree with.
        got = []
        for tag in ("aim",):
            for e in RETICLE:
                g.quiet()                       # nothing in view: the sight
                m.advance(frames=25)            # opens and tk_lockwas clears,
                g.place(TK_NSTAT, e, RANGE, SIN)   # so HYSTERESIS is not in play
                m.advance(frames=25)
                err, rng = g.sw("tk_aimq"), g.w("tk_aimz")
                err = abs(err - fix(err, g.cap(), BOXQ))    # what is LEFT
                win = HITQ // max(1, rng)
                lock = g.b("tk_locked")
                ok = lock == (1 if err <= win else 0)
                got.append(lock)
                print("  %-4s: tank %+.2f units, %d out -> %d quarters STILL "
                      "wrong after the gun, window %d, sight %s%s"
                      % (tag, e, rng, err, win, "CLOSED" if lock else "open",
                         "" if ok else "   <-- disagrees"))
                if not ok and abs(err - win) > HYS:
                    bad.append("%s at %+.2f units: the sight is %s with a "
                               "residual of %d quarters against a window of "
                               "%d - it is promising a shot the gun does not "
                               "keep, or hiding one it does (SPEC.md 85.6.5.7)"
                               % (tag, e, "closed" if lock else "open",
                                  err, win))
        if not (any(got) and not all(got)):
            bad.append("the reticle ladder came back all %s: it agreed with "
                       "itself and tested nothing"
                       % ("closed" if all(got) else "open"))

    if bad:
        print("\ntankaim: FAIL")
        for s in bad:
            print("  - " + s)
        return 1
    print("\ntankaim: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
