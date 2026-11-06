#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcpacghost.py - Pac-Man's ghost states all have code
# (ARCADE-SPEC 7.6, tools/arcpac.py seeds' RST 20h tables)
#
#     python3 tests/arcpacghost.py [--package build/arcade.o88]
#
# Pac-Man dispatches each ghost's state through an RST 20h jump table
# (10B7h's for the fourth: 000Ch, 11C9h, 11DBh, 11FCh by 4DAFh). The seeds
# were learned from a scripted run, and one that never ate a ghost never
# reached state 2 - 11DBh had no translation, and ARCADE stopped with "no
# code at 11DB" halfway through a first level (the owner's field report,
# 2026-11-06). The seeds now read every table entry statically.
#
# On 86Box's mr286 (kern_big: an XT plays Pac-Man a frame or two a second,
# too slow to reach play): Pac-Man picked, a coin and a start, play confirmed
# (4E00h = 3), then each ghost's state set to 2 in turn - frames must go on
# at the board's rate and the game stay in play. Red with the parts from
# before the static tables: 0 frames, back at the cabinet at the first.
# =============================================================================
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
os.environ.setdefault('OS88_EMU', '86box')
import os88build                    # noqa: E402
import os88marty as M               # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

V_NFRAME = 0x6044
GHOST_ST = 0x4DAC                   # the four ghosts' states, 4DACh-4DAFh
MAIN_ST = 0x4E00                    # 3: a game in play


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--package', default=os88build.at('build/arcade.o88'))
    a = ap.parse_args()
    td = tempfile.mkdtemp(prefix='arcpacghost-')
    shutil.copy(a.package, os.path.join(td, 'ARCADE.O88'))
    disk = os.path.join(td, 'pac.img')
    subprocess.run([sys.executable, 'tools/os88disk.py', '-o', disk, '--size', '1440',
                    '--folder', 'SYSTEM/APPDATA', os.path.join(td, 'ARCADE.O88'),
                    os88build.at('build/arcade/roms/PACMAN.ROM')],
                   check=True, capture_output=True)
    bad = []
    with os88ui.boot(os88build.at('build/os8088.img'), apps=disk, machine='at286_vga',
                     quick=True) as ui:
        m = ui.m

        def rd(off, n=1):
            return bytes(m.read(find_view(m) + off, n))

        def frames():
            return struct.unpack('<H', rd(V_NFRAME, 2))[0]
        ui.path('B:/ARCADE.O88')
        m.run()
        M.guest_sleep(m, 1)
        ui.menu_pick('Game', 'Pac-Man')
        for _ in range(120):
            M.guest_sleep(m, 0.5)
            if frames() > 30:
                break
        else:
            raise SystemExit('arcpacghost: FAIL - Pac-Man never ran')
        M.guest_sleep(m, 6)
        for k in ('Digit5', 'Digit1'):  # (held: the board reads them a frame)
            m.key(k, down=True, up=False)
            M.guest_sleep(m, 0.3)
            m.key(k, down=False, up=True)
            M.guest_sleep(m, 2)
        M.guest_sleep(m, 6)
        st = rd(MAIN_ST)[0]
        print('arcpacghost: in play (4E00h = %d)' % st)
        if st != 3:
            raise SystemExit('arcpacghost: FAIL - the game is not in play (4E00h = %d)' % st)
        for g in (3, 2, 1, 0):
            m.pause()
            m.write(find_view(m) + GHOST_ST + g, bytes([2]))
            m.run()
            f0 = frames()
            M.guest_sleep(m, 6)
            n, st = frames() - f0, rd(MAIN_ST)[0]
            print('arcpacghost: ghost %d in state 2: %d frames in 6 s, 4E00h = %d' % (g, n, st))
            if n < 200 or st != 3:
                bad.append('ghost %d in state 2 stopped the board (%d frames, 4E00h = %d)'
                           % (g, n, st))
    for b in bad:
        print('arcpacghost: FAIL - ' + b)
    if bad:
        return 1
    print('arcpacghost: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
