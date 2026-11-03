#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcspeed.py - ARCADE keeps the board's time on a 4.77 MHz 8088
# (docs/ARCADE-SPEC.md sections 5 and 6.2)
#
#     python3 tests/arcspeed.py [--machine os8088_5150_cga] [--secs 10]
#
# Boots the system disk with build/arcade360.img in B: on MartyPC, opens
# ARCADE.O88, plays full screen - a coin, a start, fire and move - and reads
# the host's counters out of the view (found by its 'ARCV' signature,
# ARCADE-SPEC 2.1) across SECS guest seconds of play:
#
#   * frames delivered (RST 2s) within 3% of the board's 59.54 a second -
#     the game keeps its own time however slow the host;
#   * at most one half frame dropped to catch up;
#   * and Esc ends the bracket: no run of the translated code after it.
#
# Guest seconds are MartyPC's CYCLES at 4.77273 MHz, never the host's clock:
# a loaded host cannot move them (CLAUDE.md, Testing).
# =============================================================================
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.chdir(ROOT)
import os88ui                       # noqa: E402

MHZ = 4.77273
BOARD_HZ = 59.54


def find_view(m):
    mem = bytes(m.read(0, 0xA0000))
    i = 0
    while True:
        i = mem.find(b'ARCV', i + 1)
        if i < 0:
            raise SystemExit('arcspeed: FAIL - no ARCADE view in guest memory')
        if (i - 0x4040) % 16 == 0:
            return i - 0x4040


def counters(m, v):
    frames, drops, runs, budget, cells = struct.unpack('<5H', bytes(m.read(v + 0x4044, 10)))
    return {'frames': frames, 'drops': drops, 'runs': runs, 'cells': cells}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_5150_cga')
    ap.add_argument('--secs', type=float, default=10.0)
    ap.add_argument('--system', default='build/os8088-360.img')
    ap.add_argument('--apps', default='build/arcade360.img')
    a = ap.parse_args()
    bad = []
    with os88ui.boot(a.system, apps=a.apps, machine=a.machine, quick=True) as ui:
        m = ui.m
        ui.path('B:/ARCADE.O88')
        m.key('Enter')
        m.advance(frames=300)               # the board's power-on
        m.key('Digit5')
        m.advance(frames=300)
        m.key('Digit1')
        m.advance(frames=300)
        v = find_view(m)
        c0 = m.status()['cycles']
        k0 = counters(m, v)
        step = 0
        while (m.status()['cycles'] - c0) / (MHZ * 1e6) < a.secs:
            key = 'ArrowLeft' if (step // 3) % 2 else 'ArrowRight'
            m.key('Space')
            m.key(key, down=True, up=False)
            m.advance(frames=30)
            m.key(key, down=False, up=True)
            step += 1
        dt = (m.status()['cycles'] - c0) / (MHZ * 1e6)
        k = counters(m, v)
        fps = ((k['frames'] - k0['frames']) & 0xFFFF) / dt
        drops = (k['drops'] - k0['drops']) & 0xFFFF
        print('arcspeed: %.1f guest s of play on %s: %.2f frames/s (board %.2f), '
              '%d dropped, %.0f runs/s, %.0f cells/s'
              % (dt, a.machine, fps, BOARD_HZ, drops,
                 ((k['runs'] - k0['runs']) & 0xFFFF) / dt,
                 ((k['cells'] - k0['cells']) & 0xFFFF) / dt))
        if abs(fps - BOARD_HZ) > BOARD_HZ * 0.03:
            bad.append('frames %.2f/s, not within 3%% of %.2f' % (fps, BOARD_HZ))
        if drops > 1:
            bad.append('%d half frames dropped' % drops)
        m.key('Escape')
        m.advance(frames=30)
        r0 = counters(m, v)['runs']
        m.advance(frames=60)
        if counters(m, v)['runs'] != r0:
            bad.append('the translated code still runs after Esc')
    if bad:
        for b in bad:
            print('arcspeed: FAIL - ' + b)
        return 1
    print('arcspeed: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
