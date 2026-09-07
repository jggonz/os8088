#!/usr/bin/env python3
"""PACCMAN's generated tables say what they claim to (SPEC.md 91).

    python3 tests/unit/t_paccman.py

HOST-SIDE AND FAST, so it runs inside every `make`. It reads the COMMITTED
apps/paccman/pmc_rom.c - the build's truth - and asserts the handful of facts
about it that would otherwise be checked by looking at a screendump and
believing it.

WHY EACH ROW EXISTS:

  the maze          240 dots + 4 pills = 244 is the arcade's own count and is
                    the number every ghost-house dot counter and the
                    round-won test is written against. A char->tile table
                    edited by hand is exactly the shape of thing that loses a
                    row and still draws a maze that LOOKS right.
  the door          the two ghost-house door tiles at (13,15) and (14,15) are
                    what the ghosts leave through and what game_init_playfield
                    colours 0x18; a maze without them traps four ghosts
                    forever and the failure looks like an AI bug.
  the tunnel        row 17 must be open at both ends, or the wrap has nothing
                    to wrap through.
  blocking          the reference decides "wall" as tile >= 0xC0, so no dot,
                    pill, space or letter tile may land there and no wall
                    tile below it.
  the pin           pmc_rom.c's header carries the reference commit the
                    extractor is pinned to. Drift here means the committed
                    tables and the tool that regenerates them disagree about
                    what they are.
  the sizes         every table is the length its name promises: an extractor
                    that half-wrote one would otherwise be found by a garbled
                    tile on the glass, three waves later.
  the melody        the prelude's MELODY is voice 1 of the dump (539 then
                    1078 Hz) and its BASS voice 0 (67 Hz). They have been the
                    wrong way round once; asserted BY NAME so the swap cannot
                    come back quietly, because both orderings produce sound.
  the constants     apps/paccman/pmcband.inc and apps/paccman/paccman.c each
                    state the band's three sizes - one from PMC_BAND_W, one
                    from PMC_FIELD_W. Two copies of one fact is a thing this
                    tree does deliberately and then guards; pmcband.inc is
                    right if they ever differ.
  reproduction     ...and the whole file against the extractor, which SKIPS
                    with the pin's name when no checkout is at $PACMANC_SRC.
                    The reference is not vendored (CONTRIBUTING.md 6), so a
                    contributor's tree cannot run this and must not fail on it.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROM = os.path.join(ROOT, "apps/paccman/pmc_rom.c")
BAND = os.path.join(ROOT, "apps/paccman/pmcband.inc")
MAIN = os.path.join(ROOT, "apps/paccman/paccman.c")
PIN = "0f5ec5a384c1988d9889046d92e615219e1cf3b4"

TILE_DOT, TILE_PILL, TILE_DOOR = 0x10, 0x14, 0xCF
COLS, ROWS = 28, 31                      # the map is rows 3..33 of the field


def arrays(src):
    """Every `static const ... name[...] = { ... };` in the generated file."""
    out = {}
    for m in re.finditer(r'static const unsigned (?:char|int) (\w+)\[[^\]]*\]'
                         r'\s*=\s*\{(.*?)\n\};', src, re.S):
        out[m.group(1)] = [int(t, 0) for t in m.group(2).replace('\n', ' ')
                           .split(',') if t.strip()]
    return out


def main():
    src = open(ROM).read()
    a = arrays(src)

    # --- the pin -----------------------------------------------------------
    eq(PIN in src, True, "pmc_rom.c names the pinned reference commit",
       why="the committed tables and tools/paccman_assets.py must agree about "
           "which pacman.c they came from (SPEC.md 91)")

    # --- the sizes ---------------------------------------------------------
    for name, want in (("pmc_tiles", 256 * 16), ("pmc_sprites", 64 * 64),
                       ("pmc_pal", 32 * 4), ("pmc_pairs", 32 * 16),
                       ("pmc_mono", 16), ("pmc_mono2", 2 * 256),
                       ("pmc_planar", 256), ("pmc_zmask", 256),
                       ("pmc_maze", ROWS * COLS),
                       ("pmc_lvl_fruit", 21), ("pmc_lvl_bonus", 21),
                       ("pmc_lvl_fright", 21)):
        eq(len(a.get(name, [])), want, "%s is %d entries" % (name, want),
           why="a half-written table draws a garbled tile, three waves later")

    # --- the CGA row merge's table (SPEC.md 91) ----------------------------
    # pmc_zmask[b] must be 0b11 in every 2-bit field of b that is ZERO and 0
    # in every field that is not, because _pmc_tile's CGA arm composes
    # `merged = even | (odd & zmask[even])`. A table that answered 0b11 for a
    # NON-zero field would let the odd row overwrite the even one and the
    # merge would silently become "take the lower row", which draws a
    # plausible picture; one that answered 0 everywhere would silently be the
    # plain sample the merge replaced.
    zm = a.get("pmc_zmask", [])
    if check(len(zm) == 256, "pmc_zmask is 256 entries"):
        bad = [b for b in range(256)
               if zm[b] != sum(3 << (2 * f) for f in range(4)
                               if ((b >> (2 * f)) & 3) == 0)]
        eq(bad[:4], [], "pmc_zmask is 0b11 in exactly the zero pixel fields",
           why="the CGA row merge is `even | (odd & zmask[even])`; a wrong "
               "entry there either overwrites the even row or turns the "
               "merge back into the sample, and both draw a picture")

    # --- the maze ----------------------------------------------------------
    maze = a.get("pmc_maze", [])
    if check(len(maze) == ROWS * COLS, "the maze is 31 rows of 28"):
        eq(maze.count(TILE_DOT), 240, "the maze holds 240 dots",
           why="240 + 4 pills = the arcade's 244, which every dot counter and "
               "the round-won test is written against")
        eq(maze.count(TILE_PILL), 4, "the maze holds 4 energizer pills")
        eq(maze.count(TILE_DOOR), 2, "the maze holds 2 ghost-house door tiles",
           why="the ghosts leave through them; without them four ghosts are "
               "trapped and it reads as an AI bug")

        def at(x, y):                    # field row y, so map row y - 3
            return maze[(y - 3) * COLS + x]

        eq(at(13, 15), TILE_DOOR, "the ghost-house door is at (13,15)")
        eq(at(14, 15), TILE_DOOR, "...and at (14,15)")
        eq(at(0, 17) < 0xC0, True, "the tunnel row is open at the left",
           why="row 17 wraps at x < 0 and x >= 224; a wall either end leaves "
               "nothing to wrap through")
        eq(at(27, 17) < 0xC0, True, "...and at the right")

        # No dot, pill or door may read as a wall, and no wall as walkable.
        bad = [i for i, t in enumerate(maze)
               if t in (TILE_DOT, TILE_PILL) and t >= 0xC0]
        eq(bad, [], "no dot or pill sits at a blocking tile code",
           why="the reference decides 'wall' as tile >= 0xC0 (pacman.c 1260+)")

    # --- the prelude's two voices ------------------------------------------
    mel = a.get("pmc_snd_prelude_hz1", [])
    bass = a.get("pmc_snd_prelude_hz0", [])
    if check(len(mel) == 245 and len(bass) == 245,
             "both prelude voices are 245 ticks"):
        eq(mel[0], 539, "the prelude MELODY starts at 539 Hz on voice 1",
           why="the melody is voice 1 and the bass voice 0; they have been the "
               "wrong way round once, and BOTH orderings produce sound")
        eq(mel[8], 1078, "...and its second note is 1078 Hz")
        eq(bass[0], 67, "the prelude BASS is 67 Hz on voice 0")

    # --- pixel 0 of every colour block -------------------------------------
    pal = a.get("pmc_pal", [])
    if check(len(pal) == 128, "the palette is 32 blocks of 4"):
        eq([i for i in range(32) if pal[i * 4] != 0], [],
           "pixel 0 of every colour block is black",
           why="it is the arcade's transparent index: a tile draws it as the "
               "field's own ground and a sprite skips it")

    # --- pmc_pairs is pmc_pal, folded ---------------------------------------
    pairs = a.get("pmc_pairs", [])
    if check(len(pairs) == 512 and len(pal) == 128,
             "the pair table and the palette are both present"):
        bad = [(c, n) for c in range(32) for n in range(16)
               if pairs[c * 16 + n] != (pal[c * 4 + (n >> 2)] << 4)
                                       | pal[c * 4 + (n & 3)]]
        eq(bad, [], "pmc_pairs is pmc_pal folded into pixel pairs",
           why="pmcband.inc's XLAT reaches a colour through pairs alone, so a "
               "pair table that disagrees with the palette silently recolours "
               "the whole field")

    # --- ONE FACT, TWO FILES ------------------------------------------------
    band = open(BAND).read()
    main_c = open(MAIN).read()
    # BAND_ROW is the row PITCH and BAND_STRIDE the row WIDTH, and the two
    # differ by PMC_BAND_PAD at each end - eight pixels of slack a sprite at
    # the tunnel mouth hangs into, which is what lets _pmc_sprite never clip
    # (SPEC.md 91). Getting those two the wrong way round in one of the two
    # files would compose every band at the wrong pitch, which is the kind of
    # thing that draws a plausible picture and is wrong everywhere.
    want = {"BAND_STRIDE": 112, "BAND_PAD": 4, "BAND_ROW": 120,
            "PL_STRIDE": 28, "PL_STEP": 224}
    for key, n in want.items():
        m = re.search(r'^PMC_%s\s+equ\s+(.+?)\s*(?:;|$)' % key, band, re.M)
        if not check(m, "pmcband.inc defines PMC_%s" % key):
            continue
        # `(PMC_BAND_W / 2)` and friends: evaluate with PMC_BAND_W = 224
        expr = m.group(1).replace("PMC_BAND_W", "224") \
                         .replace("PMC_BAND_STRIDE", "112") \
                         .replace("PMC_BAND_PAD", "4") \
                         .replace("PMC_PL_STRIDE", "28")
        eq(eval(expr, {"__builtins__": {}}), n,
           "pmcband.inc's PMC_%s is %d" % (key, n))
        m2 = re.search(r'#define\s+PMC_%s\s+\(([^)]*\))?' % key, main_c)
        eq(bool(re.search(r'#define\s+PMC_%s\b' % key, main_c)), True,
           "paccman.c mirrors PMC_%s" % key,
           why="two copies of one fact; if they differ, pmcband.inc is right")

    # --- AND THE ABOUT CARD'S TWO NUMBERS, WHICH ARE SOMEBODY ELSE'S --------
    # pmc_menu.c's pmc_ab_mark re-marks only the bands the About card covered
    # rather than all 36 (~2.5 s of XT from an ordinary keystroke), and it can
    # only do that by mirroring the widget's own measurement: apps/os88ui.inc
    # sizes the card at `lines * OS88UI_ABLH + 2 * OS88UI_ABPADY`, clamped to
    # the content box. Those two equs are not PACCMAN's to change and nothing
    # else would notice if they moved - the card would simply be measured
    # wrongly by a package, and the residue would be a strip of card left on
    # the field, on one adapter, after a keystroke. So they are pinned here.
    # The line pitch also appears in pmc_ab_box as `(n << 3) + (n << 2)`,
    # because tools/cc8086.py refuses `imul ax, ax, 12`.
    #
    # PMC_AB_PADX joined them when pmc_repaint began marking the COMPLEMENT of
    # the card on a whole repaint (SPEC.md 91): a complement has COLUMNS in it,
    # so the card's width is measured too, and a wrong PADX there leaves a
    # stale strip of field beside the card rather than a strip of card on the
    # field - the same defect with its sign the other way.
    ui = open(os.path.join(ROOT, "apps/os88ui.inc")).read()
    menu_c = open(os.path.join(ROOT, "apps/paccman/pmc_menu.c")).read()
    for key, name in (("ABLH", "LH"), ("ABPADY", "PADY"), ("ABPADX", "PADX")):
        m = re.search(r'^OS88UI_%s\s+equ\s+(\d+)' % key, ui, re.M)
        if not check(m, "apps/os88ui.inc defines OS88UI_%s" % key):
            continue
        m2 = re.search(r'^#define\s+PMC_AB_%s\s+(\d+)' % name, menu_c, re.M)
        if not check(m2, "pmc_menu.c mirrors it as PMC_AB_%s" % name):
            continue
        eq(int(m2.group(1)), int(m.group(1)),
           "PMC_AB_%s mirrors OS88UI_%s (%s)" % (name, key, m.group(1)),
           why="the About card's own measurement; if they differ, "
               "apps/os88ui.inc is right and pmc_ab_mark leaves card residue")
    lh = re.search(r'#define\s+PMC_AB_LH\s+(\d+)', menu_c)
    sh = re.search(r'h\s*=\s*\(n\s*<<\s*(\d+)\)\s*\+\s*\(n\s*<<\s*(\d+)\)', menu_c)
    if check(lh and sh, "pmc_ab_box multiplies the line count by a shift pair"):
        eq((1 << int(sh.group(1))) + (1 << int(sh.group(2))), int(lh.group(1)),
           "the shift pair is PMC_AB_LH (%s)" % lh.group(1),
           why="the constant says the fact and the shifts do the arithmetic, "
               "because tools/cc8086.py refuses `imul ax, ax, 12`; if they "
               "drift the card is measured at the wrong pitch")

    # --- and the whole file against the extractor, when that is possible ----
    ref = os.environ.get("PACMANC_SRC")
    if ref and os.path.exists(ref):
        r = subprocess.run([sys.executable, "tools/paccman_assets.py",
                            "--check", "--ref", ref],
                           cwd=ROOT, capture_output=True, text=True)
        eq(r.returncode, 0, "pmc_rom.c reproduces from the pinned reference",
           why=(r.stdout + r.stderr).strip()[:400])
    else:
        print("t_paccman: SKIP the reproduction row - $PACMANC_SRC names no "
              "checkout of github.com/floooh/pacman.c at %s.\n"
              "           The committed apps/paccman/pmc_rom.c is the build's "
              "truth; nothing is vendored (CONTRIBUTING.md 6)." % PIN[:7])

    done("t_paccman")


if __name__ == "__main__":
    main()
