#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcbuilt.py - what a pick BUILDS from the user's ROM is what
# the host tools build from it (docs/plans/ARCADE-RELEASE-PLAN.md 2.4)
#
#     python3 tests/arcbuilt.py [--machine os8088_xt_vga] [--game Q*bert ...]
#
# Pac-Man's and Q*bert's ROM parts are no longer shipped: pm_build and
# qb_build make them on the guest, in 8086, from the .ROM. The host tools
# that made them before (apps/arcade/arcpmgfx.py, arcqbgfx.py) still run in
# the build, and are the reference: the game is picked on MartyPC, and the
# claims the pick built (ar_rseg, and Q*bert's ar_aseg) are read back and
# compared byte for byte with the host's files - Q*bert's ROM part to 8800h,
# where the host's file keeps a list of the yields the guest takes from
# qbpatch.inc instead.
# =============================================================================
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                    # noqa: E402
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

# game: [(the variable holding the segment, the host's file, the ranges)]
WANT = {
    'Q*bert': [('ar_rseg', 'build/arcade/QBROM.PRT', ((0, 0x8800), (0x8840, 0x8940))),
               ('ar_aseg', 'build/arcade/QBSPR.PRT', ((0, 0xA000),))],
    'Pac-Man': [('ar_rseg', 'build/arcade/PACMAN.PRT', ((0, 0xBF00),))],
}


def var_addr(name):
    """A bss cell's offset, out of the listing's encoding of `mov [name], ax`
    (A3 lo hi) or `mov ax, [name]` (A1 lo hi)."""
    for ln in open(os88build.at('build/arcade.lst')):
        m = re.match(r'\s*\d+\s+[0-9A-F]{8}\s+A[13]\[([0-9A-F]{4})\]\s.*\[%s\]' % name, ln)
        if m:
            h = m.group(1)
            return int(h[2:4] + h[0:2], 16)
    raise SystemExit('arcbuilt: FAIL - %s is not in the listing' % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_xt_vga')
    ap.add_argument('--game', action='append')
    a = ap.parse_args()
    games = a.game or ['Q*bert', 'Pac-Man']
    games = [g for g in games
             if all(os.path.exists(os88build.at(f)) for _, f, _ in WANT[g])]
    bad = []
    with os88ui.boot('build/os8088-360.img', apps='build/arcade360.img',
                     machine=a.machine, quick=True) as ui:
        m = ui.m
        ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        m.pause()
        img = open(os88build.at('build/arcade.bin'), 'rb').read()
        mem = bytes(m.read(0, 0xA0000))
        base = mem.find(img[0xE00:0xE80]) - 0xE00
        v = find_view(m)
        for g in games:
            m.write(v + 0x6044, b'\0\0')   # (the frames, counted afresh)
            m.run()
            ui.menu_pick('Game', g)
            v = find_view(m)                # the pick reads a floppy and builds:
            for _ in range(120):            #   wait for the board to RUN (its
                os88marty.guest_sleep(m, 0.5)   # frames counted, in the view)
                if struct.unpack('<H', bytes(m.read(v + 0x6044, 2)))[0] > 30:
                    break
            else:
                raise SystemExit('arcbuilt: FAIL - %s never ran' % g)
            m.pause()
            mem = bytes(m.read(0, 0xA0000))  # (the package's region is MOVABLE:
            base = mem.find(img[0xE00:0xE80]) - 0xE00   # a pick's claims move it)
            for var, host, n in WANT[g]:
                seg = struct.unpack("<H", bytes(m.read(base + var_addr(var), 2)))[0]
                top = n[-1][1]
                got = bytes(m.read(seg * 16, top))
                want = open(os88build.at(host), 'rb').read()[:top]
                diff = [i for lo, hi in n for i in range(lo, hi) if got[i] != want[i]]
                n = sum(hi - lo for lo, hi in n)
                print('arcbuilt: %s %s: %d of %d bytes differ%s'
                      % (g, os.path.basename(host), len(diff), n,
                         (' (first at %04X: %02X, not %02X)' % (diff[0], got[diff[0]], want[diff[0]]))
                         if diff else ''))
                if diff:
                    bad.append('%s: %s differs from the host\'s' % (g, os.path.basename(host)))
            m.run()
            m.key('Escape')
            os88marty.guest_sleep(m, 20)    # (a first leave takes the game's
                                            #   picture and writes it: ARCADE-
                                            #   SPEC 11.4, seconds on an 8088)
    for b in bad:
        print('arcbuilt: FAIL - ' + b)
    if bad:
        return 1
    print('arcbuilt: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
