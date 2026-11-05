#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcopt.py - ARCADE's settings, scores, pause and help
# (ARCADE-SPEC 12) on MartyPC's VGA XT
#
#   - SETTINGS (F2) on Space Invaders, BASES turned to 5: the choice kept in
#     ARCADE.DAT (read back off the floppy) and in the board's DIP byte at
#     the next power-on - IN 2's bits 0-1 = 2;
#   - P holds the board: its frame count stands still for two guest seconds,
#     and any key plays on; F1 holds it with the card shown, the same;
#   - a best score: put in the game's RAM (20F4h) as it is played, kept in
#     ARCADE.DAT as it is left, and put back after the next power-on while
#     the game's own reads zero;
#   - Esc from a hold goes back to the list.
# =============================================================================
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcimport import fat_cat       # noqa: E402
from arcspeed import find_view      # noqa: E402

V_IN2, V_NFRAME = 0x601A, 0x6044
DATSZ = 1 + 8 * 3 + 8 * (2 + 4)     # ARCADE.DAT: places, drives, switches, bests
DSV, HSV = 1 + 8 * 3 + 8, 1 + 8 * 3 + 16


def main():
    bad = []
    td = tempfile.mkdtemp(prefix='arcopt-')
    with os88ui.boot('build/os8088-360.img', apps='build/arcade360.img',
                     machine='os8088_xt_vga', quick=True) as ui:
        m = ui.m

        def sleep(s):
            os88marty.guest_sleep(m, s)

        def view_read(off, n):
            m.pause()
            v = find_view(m)
            b = bytes(m.read(v + off, n))
            m.run()
            return b

        def frames():
            return struct.unpack('<H', view_read(V_NFRAME, 2))[0]

        def dat():
            m.pause()
            back = os.path.join(td, 'b.img')
            m.flush(1, back)
            m.run()
            return fat_cat(back, 'SYSTEM/APPDATA/ARCADE.DAT')
        ui.path('B:/ARCADE.O88')
        m.run()
        sleep(2)
        for k in ('F2', 'ArrowRight', 'ArrowRight', 'F2'):   # BASES: 3, 4, 5
            m.key(k)
            sleep(1)
        sleep(2)
        d = dat()
        if not d or len(d) != DATSZ or d[DSV] != 0x02:
            bad.append('ARCADE.DAT: %s, switches %s' % (len(d) if d else 'MISSING',
                                                        hex(d[DSV]) if d and len(d) > DSV else '?'))
        else:
            print('arcopt: SETTINGS, BASES 5: kept in ARCADE.DAT (%d bytes, switches %02Xh)'
                  % (len(d), d[DSV]))
        m.key('Enter')
        sleep(8)
        in2 = view_read(V_IN2, 1)[0]
        print('arcopt: played: IN 2 = %02Xh (bits 0-1: %d, so %d bases)' % (in2, in2 & 3, 3 + (in2 & 3)))
        if in2 & 0x0B != 0x02:
            bad.append('IN 2 is %02Xh: the switches did not reach the board' % in2)
        for key, what in (('KeyP', 'P'), ('F1', 'F1')):
            m.key(key)
            sleep(1)
            f0 = frames()
            sleep(2)
            f1 = frames()
            m.key('KeyX')
            sleep(2)
            f2 = frames()
            print('arcopt: %s: %d frames in 2 held seconds, %d in 2 after a key' % (what, f1 - f0, f2 - f1))
            if f1 != f0:
                bad.append('%s did not hold the board (%d frames)' % (what, f1 - f0))
            if f2 - f1 < 60:
                bad.append('after %s the board did not play on (%d frames)' % (what, f2 - f1))
        m.pause()                       # a best: 0123 in the game's RAM
        v = find_view(m)
        m.write(v + 0x20F4, b'\x23\x01')
        m.run()
        sleep(0.5)
        m.key('KeyP')
        sleep(1)
        m.key('Escape')                 # (Esc from a hold: back to the list)
        sleep(6)
        d = dat()
        hs = d[HSV:HSV + 2] if d and len(d) == DATSZ else b''
        print('arcopt: left from a hold: best kept as %s' % hs.hex())
        if hs != b'\x23\x01':
            bad.append('the best was not kept in ARCADE.DAT (%s)' % hs.hex())
        ui.menu_pick('Game', 'Reset')   # power on again, and play
        sleep(1)
        m.key('Enter')
        got = None
        for _ in range(30):
            sleep(1)
            got = view_read(0x20F4, 2)
            if got == b'\x23\x01':
                break
        print('arcopt: after a power-on, the game\'s best reads %s' % got.hex())
        if got != b'\x23\x01':
            bad.append('the best was not put back (%s)' % got.hex())
    for b in bad:
        print('arcopt: FAIL - ' + b)
    if bad:
        return 1
    print('arcopt: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
