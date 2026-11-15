#!/usr/bin/env python3
"""WireArena's CAPTURE THE FLAG LINKED, wave C3 (docs/plans/
completed/WIREARENA-CTF-PLAN.md 7): two QEMUs on a socket cable, the shipped package,
walink's harness (tests/walink.py: MartyPC cannot link two instances).

  - THE MODE: the host's Mode at CTF reaches the guest in its HELLO, and
    both play CTF on the host's arena (the Twin);
  - THE TEAMS: the host (slot 0) red, the guest (slot 1) blue - each spawns
    on its own side (the Twin's west square red's, the east blue's);
  - THE GUEST TAKES: the guest's player at red's base - the HOST's rules
    see it there (its SNAP) and give it red's flag; the FLAG event tells the
    guest, whose flag line says it carries it ("YOU HAVE THE FLAG");
  - THE GUEST SCORES: at its own base with it, blue has 1 on both machines
    and red's flag is home on both;
  - THE HOST TAKES: the host at blue's base carries blue's flag; on the
    guest that flag is carried by figure 0, the host, and drawn over it.

Red with the guest's FLAG events ignored (wc_evin returning at once: the
guest never carries, never scores), and with the host's sending off
(wc_send returning at once: the same, from the other side).
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

RED, BLUE = (192, 1024), (1856, 1024)
WC_SZ = 10


def flag(m, k):
    d = m.readseg(m.seg, m.sy['wa_cfl'] + k * WC_SZ, WC_SZ)
    x, y, z = struct.unpack_from('<hhh', d, 2)
    return dict(st=d[0], car=d[1], x=x, y=y, z=z)


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
        print('wactflink: FAIL - %d of the checks:' % len(L.fails))
        for f in L.fails:
            print('  ' + f)
        sys.exit(1)
    print('wactflink: ok - the mode by HELLO, the teams, side spawns, the '
          'guest\'s take and capture, the host\'s take seen on the guest')


def run(a, b):
    check = L.check
    for m in (a, b):
        m.wait_for(lambda m=m: m.v('wa_lkpeer', 4) != 0 and
                   m.v('lk_blk') == 1, 'THE LOBBY: a peer found', secs=20)
    ida, idb = a.v('wa_lkid', 4), b.v('wa_lkid', 4)
    host, guest = (a, b) if ida < idb else (b, a)
    host.put([('lkarena', 4, 1), ('lkbots', 0, 1), ('lkgmode', 1, 1)])
    Q.pace(host, 1.0)                   # (a HELLO or two: the guest hears)
    for m in (a, b):
        m.key('spc')
    for m in (a, b):
        m.wait_for(lambda m=m: m.v('wa_lkon') == 1,
                   'THE LOBBY: %s in the match'
                   % ('the host' if m is host else 'the guest'), secs=15)
    Q.pace(host, 1.0)
    check(host.v('wa_gmode') == 1 and guest.v('wa_gmode') == 1 and
          guest.v('wa_arena') == 4,
          'THE MODE: CTF on both, on the host\'s arena (%d, %d, arena %d)'
          % (host.v('wa_gmode'), guest.v('wa_gmode'), guest.v('wa_arena')))
    check(host.v('wa_tm') == 0 and guest.v('wa_tm') == 1,
          'THE TEAMS: the host red, the guest blue')
    hx, gx = host.v('wa_px', 2), guest.v('wa_px', 2)
    check(hx < 1024 < gx, 'SIDE SPAWNS: the host west (%d), the guest east '
          '(%d)' % (hx, gx))

    host.at(700, 1300)                  # (the host out of the way)
    guest.at(RED[0], RED[1])            # THE GUEST TAKES
    guest.wait_for(lambda: flag(guest, 0)['st'] == 1 and
                   flag(guest, 0)['car'] == 0,
                   'THE GUEST TAKES: red\'s flag its own to carry, on the '
                   'guest (the host\'s rules, its FLAG event)', secs=8)
    check(flag(host, 0)['st'] == 1 and flag(host, 0)['car'] == 1,
          'THE GUEST TAKES: ...and on the host, carried by figure 0')
    check(guest.v('wa_cmsg', 2) == guest.sy['wc_m_you'],
          'THE GUEST TAKES: "YOU HAVE THE FLAG" on the guest')
    guest.at(BLUE[0], BLUE[1])          # THE GUEST SCORES
    guest.wait_for(lambda: caps(guest) == (0, 1),
                   'THE GUEST SCORES: blue 1 on the guest', secs=8)
    check(caps(host) == (0, 1) and flag(host, 0)['st'] == 0 and
          flag(guest, 0)['st'] == 0,
          'THE GUEST SCORES: blue 1 on the host, red\'s flag home on both '
          '(%r, %d, %d)' % (caps(host), flag(host, 0)['st'],
                            flag(guest, 0)['st']))
    guest.at(1700, 1300)                # (the guest away)
    host.at(BLUE[0], BLUE[1])           # THE HOST TAKES
    guest.wait_for(lambda: flag(guest, 1)['st'] == 1 and
                   flag(guest, 1)['car'] == 1,
                   'THE HOST TAKES: blue\'s flag carried by figure 0 (the '
                   'host), on the guest', secs=8)
    Q.pace(guest, .5)
    f, fig = flag(guest, 1), guest.fig(0)
    check(abs(f['x'] - fig['x']) < 64 and abs(f['y'] - fig['y']) < 64,
          'THE HOST TAKES: ...and drawn over it there (%d, %d by %d, %d)'
          % (f['x'], f['y'], fig['x'], fig['y']))


if __name__ == '__main__':
    main()
