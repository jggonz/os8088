#!/usr/bin/env python3
"""PIXELSTEIN 3D's levels hold the wave-3 rules (SPEC.md 97.6, 97.7).

    python3 tests/unit/t_pxsmap.py

Host-side, soak (`soak -k 't_pxs*'`). Every level under apps/pixelstein/levels/
passes tools/pxslevel.py's check() WITH the DDA sweep in both door states
(the `pxs-level` row runs the tool as a process; this one calls the rules
and reads their numbers), and the two rules wave 3 leans on are asserted by
name: THE MELEE INVARIANT - no open cell has more than two guards within
1.5 tiles of it at spawn, which is what bounds the frame the sprite cap
exists for (97.6) - and the doors in cell order, which the engine's
per-row door table (px_door_of) is built from. Three floors at least
(e1m1..e1m3, wave 3's), and a patroller where a level carries one. The
NEGATIVE CONTROL is a level with three guards at one cell's melee reach,
refused in words naming the cell.
"""
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pxslevel                                             # noqa: E402

MELEE = """\
##########
#........#
#.@......#
#........#
#....ggg.#
#........#
#....X...#
##########
"""
# a door that sits in a wall (solid north and south of it) and opens INTO
# one (its east cell): review r1 found three in E1M1, the gold door among them
DOORWALL = """\
##########
#....#...#
#.@..D####
#....#...#
#....#..X#
##########
"""


def main():
    paths = pxslevel.level_paths([])
    # THE EPISODE IS EIGHT FLOORS (wave 4, SPEC.md 97.13): e1m1..e1m8 by name
    # and nothing else - deleting a floor, or adding a ninth the engine's
    # PXL_NLEV does not know, fails here
    names = sorted(os.path.splitext(os.path.basename(p))[0] for p in paths)
    eq(names, ["e1m%d" % i for i in range(1, 9)], "the episode is e1m1..e1m8, exactly eight floors")
    for p in paths:
        lv = pxslevel.parse(p)
        bad, sight, dda = pxslevel.check(lv, sweep=True)
        check(not bad, "%s passes every rule with the sweep (%s)" % (lv.name, bad or "clean"))
        mean, worst = dda[0], dda[1]
        check(mean <= pxslevel.DDA_MEAN and worst <= pxslevel.DDA_WORST,
              "%s: DDA mean %.1f <= %.0f, worst %d <= %d in the worse door state"
              % (lv.name, mean, pxslevel.DDA_MEAN, worst, pxslevel.DDA_WORST))
        check(sight <= pxslevel.MAX_SIGHT, "%s: the longest sight line is %d <= %d"
              % (lv.name, sight, pxslevel.MAX_SIGHT))
        cells = [y * pxslevel.MAP_W + x for x, y, _f, _l in lv.doors]
        eq(cells, sorted(cells), "%s: the doors are in cell order (px_door_of's table)" % lv.name)
        # the melee invariant, counted here as the engine's cap needs it
        guards = [(a[0] + 0.5, a[1] + 0.5) for a in lv.actors]   # guards AND
                                    # dogs (wave 6: a dog at melee is a sprite
                                    # as tall as a guard's)
        worst_n = 0
        for y in range(pxslevel.MAP_H):
            for x in range(pxslevel.MAP_W):
                if not lv.open_(x, y):
                    continue
                n = sum(1 for gx, gy in guards
                        if (gx - x - 0.5) ** 2 + (gy - y - 0.5) ** 2 <= pxslevel.MELEE_R2)
                worst_n = max(worst_n, n)
        check(worst_n <= 2, "%s: no open cell has more than two actors at melee (worst %d)"
              % (lv.name, worst_n))
        # THE MARKS NEVER MEET A MATERIAL (97.8; review, wave 3): the engine
        # keeps PXC_BLOCK / PXC_ACTOR / PXC_PLAYER (0x10 / 0x20 / 0x40) in
        # the high nibble of an OPEN cell, and a door cell - passable, but
        # its nibble a material - is guarded at every writer; so every cell
        # a mark can land on (neither SOLID nor DOOR) must carry material 0
        # out of the level tool, and the three marks must be clear of the
        # flag nibble
        mat = [i for i, c in enumerate(lv.cells)
               if not c & (pxslevel.SOLID | pxslevel.DOOR) and c >> 4]
        check(not mat, "%s: every open cell has material 0 (a mark's nibble; %d do not)"
              % (lv.name, len(mat)))
        check(all(m & 15 == 0 for m in (0x10, 0x20, 0x40)),
              "the three marks are clear of the flag nibble")
        print("  t_pxsmap: %s - %d doors, %d actors (%d patrol), %d statics, melee worst %d"
              % (lv.name, len(lv.doors), len(lv.actors),
                 sum(1 for a in lv.actors if a[2] & pxslevel.PATROL), len(lv.statics), worst_n))
    lv2 = pxslevel.parse(os.path.join(pxslevel.DEFAULT_DIR, "e1m2.txt"))
    check(any(a[2] & pxslevel.PATROL for a in lv2.actors), "E1M2 carries a patrolling guard (G)")
    # THE DOG (wave 6): kind 1, standing (h) or patrolling (H), on floors 3..8
    ndogs = 0
    for n in range(3, 9):
        lvn = pxslevel.parse(os.path.join(pxslevel.DEFAULT_DIR, "e1m%d.txt" % n))
        ndogs += sum(1 for a in lvn.actors if (a[2] & ~pxslevel.PATROL) == 1)
    check(ndogs >= 6, "floors 3..8 carry dogs (%d)" % ndogs)
    # --- the negative control: three guards within one cell's melee reach ------
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "melee.txt")
        open(p, "w").write(MELEE)
        lv = pxslevel.parse(p)
        bad = []
        pxslevel.check_melee(lv, bad)
        check(bad and "melee" in bad[0], "three guards at one cell's melee reach are REFUSED (%s)"
              % (bad[0] if bad else "accepted"))
        check(bad and "3 actors" in bad[0], "...in words naming the count")
        p = os.path.join(d, "doorwall.txt")
        open(p, "w").write(DOORWALL)
        try:
            pxslevel.parse(p)
            err = ""
        except pxslevel.LevelError as e:
            err = str(e)
        check("opens into a wall at (6,2)" in err, "a door whose far side is a wall is REFUSED "
              "naming the cell (%s)" % (err or "accepted"))
    # --- the line-of-sight slope's divisor keeps the 8086's idiv in range (r1) --
    # ((PX_LOSMAX + 1) x 256 - 1) << 8 over PX_LOSDMIN, both signs, in a
    # signed word; and under 256 there is one line to cross, so the clamp
    # to a slope of 0 is exact
    asm = open(os.path.join(ROOT, "apps", "pixelstein", "pxgame.asm")).read()
    act = open(os.path.join(ROOT, "apps", "pixelstein", "pxact.inc")).read()
    losmax = int(re.search(r"^PX_LOSMAX\s+equ\s+(\d+)", asm, re.M).group(1))
    dmin = int(re.search(r"^PX_LOSDMIN\s+equ\s+(\d+)", act, re.M).group(1))
    delta = (losmax + 1) * 256 - 1
    q = (delta << 8) // dmin
    check(q <= 32767 and -q >= -32768, "px_los_slope: (%d << 8) / %d = %d fits a signed word "
          "both ways" % (delta, dmin, q))
    check(((delta << 8) // (dmin - 1)) <= 32767 or dmin <= 256,
          "...and PX_LOSDMIN = %d is under 257 (one line to cross below it)" % dmin)
    check(dmin <= 256, "PX_LOSDMIN <= 256")
    done("t_pxsmap")


if __name__ == "__main__":
    main()
