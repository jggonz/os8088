#!/usr/bin/env python3
"""WireArena's MODES LINKED, wave M4 (docs/plans/completed/WIREARENA-MODES-PLAN.md 5,
8): two QEMUs on a socket cable, the shipped package, walink's harness
(tests/walink.py: MartyPC cannot link two instances).

  - WIN AT ON THE WIRE: the host's Mode at king of the hill and its Win at
    pick (3 points) reach the guest in the HELLO;
  - THE HILL, THE HOST'S RULES: the guest (blue) on the Twin's hill - the
    host sees it there (its SNAP) and gives blue the hill; GAME tells the
    guest, whose message says WE HAVE THE HILL;
  - A POINT: the hill held its 10 s on the host, blue 1 on both;
  - THE WIN: blue's third point - BLUE WINS on both machines;
  - THE NEXT ARENA: after the 10 s, NEXT takes both machines on to the
    heights (the next king of the hill arena after the Twin, round) - the
    scores anew and the hill nobody's on both, the link still up, and the
    guest's pickups the host's (its ROLLs, after the NEXT).

Red with the guest's GAME events ignored (wc_gin returning at once: no hill,
no point, no win on the guest) and its NEXT ignored (wc_nin returning at
once: the guest stays on the Twin).
"""
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import walink as L                                              # noqa: E402
import wireball as W                                            # noqa: E402
import os88qemu as Q                                            # noqa: E402

HILL = (1024, 832)
WC_ST, WC_TMR = 0, 8


def caps(m):
    return tuple(m.readseg(m.seg, m.sy['wa_caps'], 2))


def main():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        a, b = L.Arena(d, 0), L.Arena(d, 1)
        cable = W.Cable([a, b])
        for m in (a, b):                # the setup: Play, serial cable
            m.setup_play(1)
        try:
            run(a, b)
        finally:
            cable.close()
            a.cleanup()
            b.cleanup()
    if L.fails:
        print('wamodeslink: FAIL - %d of the checks:' % len(L.fails))
        for f in L.fails:
            print('  ' + f)
        sys.exit(1)
    print('wamodeslink: ok - Win at by HELLO, the hill by the host\'s rules, '
          'a point, the win and the next arena on both machines')


def run(a, b):
    check = L.check
    for m in (a, b):
        m.wait_for(lambda m=m: m.v('wa_lkpeer', 4) != 0 and
                   m.v('lk_blk') == 1, 'THE LOBBY: a peer found', secs=20)
    ida, idb = a.v('wa_lkid', 4), b.v('wa_lkid', 4)
    host, guest = (a, b) if ida < idb else (b, a)
    for m in (a, b):                    # (the bytes poked, by name)
        m.sy['wa_sgoal3'] = m.sy['wa_sgoal'] + 3
        m.sy['wa_hilltmr'] = m.sy['wa_cfl'] + WC_TMR
    host.put([('lkarena', 4, 1), ('lkbots', 0, 1), ('lkgmode', 3, 1),
              ('sgoal3', 1, 1)])
    Q.pace(host, 1.0)                   # (a HELLO or two: the guest hears)
    check(guest.v('wa_lkgmode') == 3 and
          guest.readseg(guest.seg, guest.sy['wa_sgoal'] + 3, 1)[0] == 1,
          'WIN AT ON THE WIRE: king of the hill and its pick, 3 points, on '
          'the guest')
    for m in (a, b):
        m.key('spc')
    for m in (a, b):
        m.wait_for(lambda m=m: m.v('wa_lkon') == 1,
                   'THE LOBBY: %s in the match'
                   % ('the host' if m is host else 'the guest'), secs=15)
    Q.pace(host, 1.0)
    check(host.v('wa_gmode') == 3 and guest.v('wa_gmode') == 3 and
          guest.v('wa_ggoal') == 3,
          'THE MODE: king of the hill on both, Win at 3 on the guest')

    host.at(400, 1300)                  # THE HILL: the guest on it
    guest.at(HILL[0], HILL[1])
    guest.wait_for(lambda: guest.v('wa_cfl') == 2,
                   'THE HILL: blue\'s on the guest (the host\'s rules, its '
                   'GAME)', secs=8)
    check(host.v('wa_cfl') == 2 and
          guest.v('wa_cmsg', 2) == guest.sy['wc_m_hwe'],
          'THE HILL: ...blue\'s on the host, WE HAVE THE HILL on the guest')
    guest.at(1700, 1300)
    host.put([('hilltmr', 295, 2)])       # A POINT: its 10 s up
    guest.wait_for(lambda: caps(guest) == (0, 1),
                   'A POINT: blue 1 on the guest', secs=8)
    check(caps(host) == (0, 1), 'A POINT: blue 1 on the host')
    host.put([('caps', 0x0200, 2), ('hilltmr', 295, 2)])  # THE WIN
    guest.wait_for(lambda: guest.v('wa_cwin', 2) > 0,
                   'THE WIN: shown on the guest', secs=8)
    check(host.v('wa_cmsg', 2) == host.sy['wc_m_bwin'] and
          guest.v('wa_cmsg', 2) == guest.sy['wc_m_bwin'],
          'THE WIN: BLUE WINS on both')
    for m in (host, guest):             # THE NEXT ARENA
        m.wait_for(lambda m=m: m.v('wa_arena') == 2 and
                   m.v('wa_cwin', 2) == 0,
                   'THE NEXT ARENA: the heights on the %s'
                   % ('host' if m is host else 'guest'), secs=20)
    Q.pace(host, 2.0)
    check(caps(host) == (0, 0) and caps(guest) == (0, 0) and
          host.v('wa_cfl') == 0 and guest.v('wa_cfl') == 0,
          'THE NEXT ARENA: the scores anew, the hill nobody\'s, on both')
    check(host.v('wa_lkon') == 1 and guest.v('wa_lkon') == 1 and
          guest.v('wa_lklost') == 0 and host.v('wa_lklost') == 0,
          'THE NEXT ARENA: the link still up')
    check(guest.v('wa_pkon') == host.v('wa_pkon') != 0,
          'THE NEXT ARENA: the guest\'s pickups the host\'s (%x, %x)'
          % (guest.v('wa_pkon'), host.v('wa_pkon')))


if __name__ == '__main__':
    main()
