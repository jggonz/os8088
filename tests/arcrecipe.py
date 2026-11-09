#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcrecipe.py - what ships in place of a translation is a
# RECIPE, and it is exactly the translation (docs/plans/ARCADE-RELEASE-PLAN.md
# 2.1; arcaot.py's make_recipe)
#
#     python3 tests/arcrecipe.py
#
# For every translated board the build made, two checks on the recipe
# (`build/arcade/<b>code.rcp`):
#   * FILLED from its pinned ROM image, it is byte for byte the code part the
#     same build translated (`<b>code.bin`) - so a recipe can never drift
#     from what the lockstep rows test;
#   * it carries NO run of 8 ROM bytes (that are not all one or two values,
#     which zero-filled code matches anywhere) - the holes are the only way
#     the ROM gets in, and they are empty.
# Host-only: no emulator. Exit 0 = every board's recipe holds.
# =============================================================================
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'apps', 'arcade'))
os.chdir(ROOT)
import os88build                    # noqa: E402
import arcaot                       # noqa: E402

BOARDS = (('inv', 'INVADERS'), ('pt2', 'INVADPT2'), ('lr', 'LRESCUE'),
          ('gf', 'GUNFIGHT'), ('sw', 'SEAWOLF'), ('pac', 'PACMAN'))
RUN = 8


def main():
    bad = []
    seen = 0
    for pre, rom in BOARDS:
        rcp = os88build.at('build/arcade/%scode.rcp' % pre)
        if not os.path.exists(rcp):
            if pre == 'pac':                # the owner's set: absent is normal
                continue
            bad.append('%s: no recipe was built' % rcp)
            continue
        recipe = open(rcp, 'rb').read()
        code = open(os88build.at('build/arcade/%scode.bin' % pre), 'rb').read()
        img = open(os88build.at('build/arcade-rom/%s.ROM' % rom), 'rb').read()
        filled = arcaot.fill_recipe(recipe, img)
        end = int.from_bytes(recipe[4:6], 'little')
        n = int.from_bytes(recipe[end:end + 2], 'little')
        if filled != code:
            k = next((i for i in range(min(len(filled), len(code))) if filled[i] != code[i]),
                     min(len(filled), len(code)))
            bad.append('%s: filled, it differs from the translation at %04X' % (pre, k))
        wins = {img[i:i + RUN] for i in range(len(img) - RUN) if len(set(img[i:i + RUN])) > 2}
        runs = [i for i in range(len(recipe) - RUN) if recipe[i:i + RUN] in wins]
        if runs:
            bad.append('%s: %d runs of %d ROM bytes in the recipe, the first at %04X'
                       % (pre, len(runs), RUN, runs[0]))
        print('arcrecipe: %-3s %6d bytes of code, %4d holes, recipe %6d bytes'
              % (pre, len(code), n, len(recipe)))
        seen += 1
    for b in bad:
        print('arcrecipe: FAIL - ' + b)
    if bad or not seen:
        return 1
    print('arcrecipe: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
