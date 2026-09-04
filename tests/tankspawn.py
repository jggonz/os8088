#!/usr/bin/env python3
"""TANK ATTACK's player can always drive (SPEC.md 85.6.6).

    python3 tests/tankspawn.py [--machine os8088_5150_cga_gla]

Reported from a 5150: "sometimes I spawn into an obstacle, and can only turn,
never drive". tk_newgame scattered the scenery uniformly over the torus and
stood the player at the origin without comparing the two, and tk_blocked
refuses a destination inside a 300-unit box of a piece - so a player starting
inside one is not slowed, they are SEALED IN: a step is 26 units and every one
of the 256 headings lands back inside the same box. Turning still worked,
because turning is tested against nothing.

TWO CLAIMS, and they are two different questions.

**No round starts inside a piece.** TK_CLEARR is enforced by reading the world
back out of the package's own bss over many fresh games - `N` presses of `N` -
and measuring every piece against the spawn. The host-side model in SPEC.md
85.6.6 says 5.27% of unfixed games are stuck; this asks the 8086 what it
actually placed.

**And a player who somehow IS inside one can still drive out** (85.6.6.2),
which the clearance makes unreachable - so the fixture pokes a piece on top of
the player and drives. That is the fail-safe's only test, and without one it is
a mechanism nobody has ever seen run.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import tank as tanktest                                     # noqa: E402
from tankaim import Game, const                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TK_NSTAT, TK_NOBJ = 12, 18
BLOCK = 300                             # tk_blocked's own box
ROUNDS = 24                             # fresh games to walk


def wrap(v):
    v &= 8191
    return v - 8192 if v >= 4096 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    CLEAR, SPEED = const("TK_CLEARR"), const("TK_SPEED")
    bad = []

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = tanktest.open_game(m)
        g = Game(m, seg, base)
        m.type_text("f")
        m.advance(frames=150)
        if g.w("tk_back") & 0xFF == 0:
            sys.exit("tankspawn: the bracket refused its mode")

        # --- 1. no round starts inside a piece -------------------------------
        worst, worstn, seen = 99999, 0, 0
        for r in range(ROUNDS):
            m.type_text("n")                    # a fresh scatter each time
            m.advance(frames=45)
            px, pz = g.w("tk_px"), g.w("tk_pz")
            for i in range(TK_NSTAT):
                if not g.b("tk_otype", i):
                    continue
                dx = abs(wrap(g.w("tk_ox", i * 2) - px))
                dz = abs(wrap(g.w("tk_oz", i * 2) - pz))
                seen += 1
                far = max(dx, dz)               # the box clears on EITHER axis
                if far < worst:
                    worst, worstn = far, r
                if far < BLOCK + SPEED:
                    bad.append("round %d: scenery %d is %d/%d from the spawn, "
                               "inside tk_blocked's %d box plus a %d-unit step "
                               "- that round cannot be driven out of "
                               "(SPEC.md 85.6.6)" % (r, i, dx, dz, BLOCK, SPEED))
        print("  %d rounds, %d pieces placed: the NEAREST any piece came to "
              "the spawn was %d (round %d), against TK_CLEARR %d and the %d "
              "that would seal the player in"
              % (ROUNDS, seen, worst, worstn, CLEAR, BLOCK + SPEED))
        if worst < CLEAR:
            bad.append("a piece landed %d from the spawn, inside TK_CLEARR "
                       "(%d): the fold in tk_newgame is not being applied"
                       % (worst, CLEAR))

        # --- 2. ...and being inside one is still not a dead end --------------
        # The clearance makes this unreachable, so it is POKED: a slab dropped
        # exactly on the player. Without 85.6.6.2 the heading changes and the
        # position does not, which is the report word for word.
        m.type_text("n")
        m.advance(frames=45)
        px, pz = g.w("tk_px"), g.w("tk_pz")
        g.wr("tk_ox", (px & 0x1FFF).to_bytes(2, "little"), 0)
        g.wr("tk_oz", (pz & 0x1FFF).to_bytes(2, "little"), 0)
        g.wr("tk_otype", [3], 0)                # OT_BLOCK, right on top of us
        m.advance(frames=30)
        m.key("KeyW", down=True, up=False)
        m.advance(frames=120)
        m.key("KeyW", down=False, up=True)
        m.advance(frames=30)
        nx, nz = g.w("tk_px"), g.w("tk_pz")
        moved = max(abs(wrap(nx - px)), abs(wrap(nz - pz)))
        print("  a slab poked onto the player, then W held: moved %d units "
              "(%d,%d -> %d,%d)" % (moved, px, pz, nx, nz))
        if moved < SPEED:
            bad.append("the player did not move a single step out of a piece "
                       "standing on them (%d units): tk_drive is refusing an "
                       "ESCAPE, which is the dead end of SPEC.md 85.6.6.2"
                       % moved)

    if bad:
        print("\ntankspawn: FAIL")
        for s in bad[:12]:
            print("  - " + s)
        return 1
    print("\ntankspawn: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
