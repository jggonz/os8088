#!/usr/bin/env python3
"""WireArena, four machines on one network (docs/plans/completed/WIREARENA-NET-PLAN.md
section 4): four QEMUs with an NE2000 each on tests/walink.py's hub relay,
which answers DHCP (10.7.0.20 to .23). The shipped package, real datagrams.

  - THE LOBBY: each machine finds the other three by the broadcast alone,
    and all four agree who hosts (the lowest identity);
  - THE SLOTS: Space on all four starts one match - the same arena and tag
    everywhere, four players, no room for the host's two bots, and the slots
    0 to 3 handed out by identity, the same on every machine;
  - SNAP: each player put somewhere is, on each of the other three, the
    figure its slot maps to there;
  - A KILL: slot 3's pistol kills slot 1's player on slot 1's machine - the
    damage word in slot 3's record for that peer only - and once a SCORE has
    crossed, every machine's table says slot 1 died once to slot 3;
  - A PLAYER LEAVES: Esc on all four, the fourth machine switched off, and
    the other three drop it from their tables once it is quiet (a quiet peer
    would hold the host's start: it waits for every peer to be READY);
  - 3 + 1: Space on the three - three players, and of the host's two bots
    the one slot left; the bot where the host has it on both guests;
  - ONE LEAVES THE MATCH: a guest's Esc - the others mark that peer alone
    as gone; then the host's - HOST LEFT, its bot off the board;
  - THE WIRE: every datagram of ours any machine sent, parsed by an
    independent decoder, is a frame of a known type and length.

--ip: THREE JOINED BY ADDRESS (NET-PLAN 6.1). The relay drops every
broadcast to WireArena's port, as a router would; B and C each type A's
address and nothing else. A's PEERS tell each about the other, and they
bind each other directly: one match of three, every player seen by both
the others, PEERS on the wire.

Registered as walink4 and walink4ip in soak; make wirearenadisk first.
"""
from pathlib import Path
import struct
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import walink as L                                              # noqa: E402
import os88qemu as Q                                            # noqa: E402

L.UDP = L.NET = True                    # (the relay answers DHCP)
IP = '--ip' in sys.argv
N = 3 if IP else 4
check = L.check


def peers(m):
    """the bound records of m's table: (index, id, state, lost, gone)"""
    out = []
    for k in range(m.v('wa_lkpn')):
        r = m.readseg(m.seg, m.sy['wa_pt'] + L.WP_SZ * k, L.WP_SZ)
        pid = struct.unpack_from('<I', r, 0)[0]
        if pid:
            out.append(dict(k=k, id=pid, ps=r[10], lost=r[14], gone=r[15],
                            seen=r[16], slot=r[31]))
    return out


def figof(m, slot):
    """the figure m draws slot `slot` as: its local code less one"""
    me = m.v('wa_lkme')
    local = slot + 1 if slot < me else slot
    return m.fig(local - 1)


def score(m, dead, cause):
    return m.v('wa_lkd', 16).to_bytes(16, 'little')[dead * 4 + cause]


def lobby_again(ms):
    """Esc on each in the match, then each sees the others back"""
    for m in ms:
        if m.v('wa_lkon'):
            m.key('esc')
            m.key('y')                 # (a linked match asks: Y leaves, NET-PLAN 9.11)
    for m in ms:
        m.wait_for(lambda m=m: m.v('wa_lkon') == 0, 'back in the window',
                   secs=6)


def main():
    L.ANIM.update(L.anims())
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        cable = L.NetCable(N)
        t = threading.Thread(target=lambda: [cable.accept(k)
                                             for k in range(N)])
        t.start()
        ms = []
        try:
            for i in range(N):
                ms.append(L.Arena(d, i, net=cable))
            t.join(30)
            cable.start()
            cable.drop_bcast = IP
            ms[0].setup_play(2)         # the setup: Play, local network
            if IP:                      # the others: Play, internet, A's
                ms[0].wait_for(lambda: ms[0].v('wa_lknet') == 2, 'NETWORK: '
                               'A on the UDP port', secs=30)
                ip = ms[0].readseg(ms[0].seg, ms[0].sy['ud_blk'], 4)
                for m in ms[1:]:        # address
                    m.setup_play(3)
                    m.key('down')
                    for ch in '.'.join(str(x) for x in ip):
                        m.key('dot' if ch == '.' else ch)
                    Q.pace(m, .3)
            else:
                for m in ms[1:]:
                    m.setup_play(2)
            (run_ip if IP else run)(ms, cable)
        finally:
            cable.close()
            for m in ms:
                m.cleanup()
    if L.fails:
        print('walink4: FAIL - %d of the checks:' % len(L.fails))
        for f in L.fails:
            print('  ' + f)
        sys.exit(1)
    print('walink4: ok - %s' % ('three machines joined by address' if IP
                                else 'four machines, one arena, the network'))


def network(ms):
    name = {id(m): 'ABCD'[i] for i, m in enumerate(ms)}
    for m in ms:
        m.wait_for(lambda m=m: m.v('wa_lknet') == 2, 'NETWORK: %s on the UDP '
                   'port' % name[id(m)], secs=30)
    return name


def wire(cable, want):
    bad, n, kinds = 0, 0, set()
    for obs in cable.observed:
        for typ, body in L.net_frames(obs):
            n += 1
            if typ is None:
                bad += 1
            else:
                kinds.add(typ)
    check(bad == 0 and n > 100 and want <= kinds,
          'THE WIRE: %d of ours, %d bad, types %s' % (n, bad, sorted(kinds)))


def run_ip(ms, cable):
    name = network(ms)
    for m in ms:
        m.wait_for(lambda m=m: len(peers(m)) == N - 1, 'BY ADDRESS: %s binds '
                   'the other two' % name[id(m)], secs=25)
    ids = [m.v('wa_lkid', 4) for m in ms]
    host = ms[min(range(N), key=lambda j: ids[j])]
    host.put([('lkarena', 2, 1), ('lkbots', 0, 1)])
    Q.pace(host, 1)
    for m in ms:
        m.key('spc')
    for m in ms:
        m.wait_for(lambda m=m: m.v('wa_lkon') == 1 and m.v('wa_lknp') == 3,
                   'BY ADDRESS: %s in a match of three' % name[id(m)],
                   secs=20)
    by = {m.v('wa_lkme'): m for m in ms}
    check(sorted(by) == [0, 1, 2], 'BY ADDRESS: slots 0..2 (%r)' % sorted(by))
    for s, m in by.items():
        m.at(1000 + 100 * s, 300 + 40 * s, 0)
    for s, m in by.items():
        for o in range(N):
            if o != s:
                m.wait_for(lambda m=m, o=o: (
                    lambda f: not f['dead'] and
                    abs(f['x'] - 1000 - 100 * o) <= 6 and
                    abs(f['y'] - 300 - 40 * o) <= 6)(figof(m, o)),
                    'BY ADDRESS: slot %d where it is, on slot %d\'s machine'
                    % (o, s), secs=6)
    wire(cable, {1, 2, 3, 8})


def run(ms, cable):
    name = network(ms)

    # THE LOBBY: each finds the other three
    for m in ms:
        m.wait_for(lambda m=m: len(peers(m)) == N - 1, 'THE LOBBY: %s binds '
                   'the other three' % name[id(m)], secs=25)
    ids = [m.v('wa_lkid', 4) for m in ms]
    check(all(sorted(p['id'] for p in peers(m)) ==
              sorted(x for x in ids if x != ids[j])
              for j, m in enumerate(ms)),
          'THE LOBBY: every table holds exactly the other three (%s)'
          % ', '.join('%08x' % x for x in ids))
    order = sorted(range(N), key=lambda j: ids[j])
    host = ms[order[0]]
    host.put([('lkarena', 2, 1), ('lkbots', 2, 1)])
    Q.pace(host, 1)                     # (its HELLO says so)

    # THE SLOTS
    for m in ms:
        m.key('spc')
    for m in ms:
        m.wait_for(lambda m=m: m.v('wa_lkon') == 1, 'THE SLOTS: Space on '
                   'all four - %s in the match' % name[id(m)], secs=20)
    check(len({(m.v('wa_arena'), m.v('wa_lktag')) for m in ms}) == 1 and
          ms[0].v('wa_lktag') != 0 and ms[0].v('wa_arena') == 2,
          'THE SLOTS: one arena and one tag (%r)'
          % [(m.v('wa_arena'), m.v('wa_lktag')) for m in ms])
    check([ms[j].v('wa_lkme') for j in order] == list(range(N)),
          'THE SLOTS: slots 0..3 by identity (%r)'
          % [m.v('wa_lkme') for m in ms])
    check(all(m.v('wa_lknp') == 4 and m.v('wa_lknb') == 0 for m in ms),
          'THE SLOTS: four players, no slot left for a bot (%r)'
          % [(m.v('wa_lknp'), m.v('wa_lknb')) for m in ms])
    slotof = {id(m): m.v('wa_lkme') for m in ms}
    check(all(sorted(p['slot'] for p in peers(m)) ==
              sorted(set(range(N)) - {slotof[id(m)]}) for m in ms),
          'THE SLOTS: each peer record has its peer\'s slot')
    by = {slotof[id(m)]: m for m in ms}

    # SNAP: slot s at (1000 + 100 s, 300 + 40 s)
    for s, m in by.items():
        m.at(1000 + 100 * s, 300 + 40 * s, 0)

    def near(m, s, x, y):
        f = figof(m, s)
        return not f['dead'] and abs(f['x'] - x) <= 6 and abs(f['y'] - y) <= 6
    for s, m in by.items():
        for o in range(N):
            if o != s:
                m.wait_for(lambda m=m, o=o: near(m, o, 1000 + 100 * o,
                                                 300 + 40 * o),
                           'SNAP: slot %d where it is, on slot %d\'s machine'
                           % (o, s), secs=6)

    # A KILL: slot 3 at (1000, 300) facing east, slot 1 ahead at 100 health;
    # 0 and 2 out of the line of fire
    shooter, victim = by[3], by[1]
    by[0].at(1000, 500, 0)
    by[2].at(1100, 500, 0)
    victim.at(1100, 300, 512)
    victim.put([('hp', 100, 2), ('sh', 0, 2)])
    shooter.at(1000, 300, 0)
    shooter.wait_for(lambda: near(shooter, 1, 1100, 300), 'A KILL: slot 1 '
                     'where slot 3 sees it')
    for _ in range(5):
        shooter.fire()
        if victim.v('wa_pdead'):
            break
    victim.wait_for(lambda: victim.v('wa_pdead') != 0, 'A KILL: slot 3\'s '
                    'pistol kills slot 1 on its own machine', secs=5)
    rec = {p['slot']: p for p in peers(shooter)}
    dout = {s: struct.unpack('<H', shooter.readseg(
        shooter.seg, shooter.sy['wa_pt'] + L.WP_SZ * rec[s]['k'] + 21, 2))[0]
        for s in rec}
    check(dout[1] >= 100 and dout[0] == dout[2] == 0,
          'A KILL: the damage in slot 3\'s record for slot 1 only (%r)' % dout)
    for s, m in by.items():
        m.wait_for(lambda m=m: score(m, 1, 3) == 1 and
                   sum(m.v('wa_lkd', 16).to_bytes(16, 'little')) == 1,
                   'A KILL: slot %d\'s table - slot 1 died once, to slot 3'
                   % s, secs=5)
    check(shooter.v('wa_kills') == 1, 'A KILL: YOU 1 kill on slot 3\'s board')
    for s in (0, 2):
        by[s].wait_for(lambda s=s: figof(by[s], 1)['dead'] != 0,
                       'A KILL: slot 1 down on slot %d\'s screen' % s, secs=3)

    # A PLAYER LEAVES: all back, the fourth switched off
    lobby_again(ms)
    gone = by[3]
    gone.cleanup()
    rest = [by[0], by[1], by[2]]
    for m in rest:
        m.wait_for(lambda m=m: len(peers(m)) == 2 and
                   all(p['ps'] == 0 for p in peers(m)),
                   'A PLAYER LEAVES: slot %d drops the quiet one, the other '
                   'two back in the lobby' % slotof[id(m)], secs=10)

    # 3 + 1
    for m in rest:
        m.key('spc')
    for m in rest:
        m.wait_for(lambda m=m: m.v('wa_lkon') == 1, '3 + 1: in the match',
                   secs=20)
    check(all(m.v('wa_lknp') == 3 and m.v('wa_lknb') == 1 for m in rest),
          '3 + 1: three players, one of the host\'s two bots (%r)'
          % [(m.v('wa_lknp'), m.v('wa_lknb')) for m in rest])
    h = by[0]

    def botmatch(g):
        hb, gb = figof(h, 3), figof(g, 3)
        return (not hb['dead'] and not gb['dead'] and
                abs(gb['x'] - hb['x']) <= 48 and abs(gb['y'] - hb['y']) <= 48)
    for s in (1, 2):
        by[s].wait_for(lambda s=s: botmatch(by[s]), '3 + 1: the bot (slot 3) '
                       'on slot %d where the host has it' % s, secs=8)

    # ONE LEAVES THE MATCH: slot 2's Esc
    by[2].key('esc')
    by[2].key('y')                 # (a linked match asks: Y leaves, NET-PLAN 9.11)
    for s in (0, 1):
        m = by[s]
        m.wait_for(lambda m=m: {p['slot']: p['gone'] for p in peers(m)} ==
                   {({0, 1} - {s}).pop(): 0, 2: 1},
                   'ONE LEAVES THE MATCH: slot %d marks slot 2 alone as gone'
                   % s, secs=6)

    # HOST LEFT: then the host's Esc - slot 1 marks slot 0 gone, which is
    # what its line says HOST LEFT on (and its bot is off the board)
    by[0].key('esc')
    by[0].key('y')                 # (a linked match asks: Y leaves, NET-PLAN 9.11)
    m = by[1]
    m.wait_for(lambda: {p['slot']: p['gone'] for p in peers(m)} ==
               {0: 1, 2: 1}, 'HOST LEFT: slot 1 marks the host gone', secs=6)
    m.wait_for(lambda: figof(m, 3)['dead'] == 250, 'HOST LEFT: its bot off '
               'slot 1\'s board', secs=3)

    wire(cable, {1, 2, 3, 5, 6, 8})


if __name__ == '__main__':
    main()
