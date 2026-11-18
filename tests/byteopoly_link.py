#!/usr/bin/env python3
"""Byteopoly over a null modem: two SHIPPED BYTEOPLY.O88 on two QEMU guests.

SPEC.md 131.5, 131.6, 131.7.5; US-16..US-20 and US-26's link half.

The `byteopolylink` soak row (tests/suite.py) runs it on the built
package and disk: `python3 tools/os88test.py soak -k byteopolylink`.

THE BRIDGE is tests/wireball.py's Cable (docs/TESTING.md item 3): the two
guests' COM ports joined by a host thread that forwards UART bytes and
nothing else. FaultRelay sits on that byte stream. It finds frames by their
7Eh delimiters and checks their CRC with binascii (never the game's own
decoder), and it may DROP, DUPLICATE, CORRUPT or DELAY a whole frame, or cut
the cable. It never chooses dice, applies a rule or repairs state.

THE PLAYERS are keys on the decider's own machine, through the shipped UI:
the setup screen's focus (read back from the guest, never assumed), the
game panels' keys (bp_panels.c) and the Game menu, picked off the
kernel's `menu_bar` and confirmed by `menu_cell`/`menu_sel` before release.
Every assertion reads the session's own record (`_bpl`, `_bp_game`) through
the shipped binary's map and compares the two guests: same revision, same
digest, nothing outstanding - at every revision.
"""
import binascii
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import wireball as W                                    # noqa: E402
import os88qemu as Q                                    # noqa: E402
import dispapps                                         # noqa: E402
import dispcp                                           # noqa: E402
from os88geom import MBAR_H, MENU_ITEM_H, MB_ENTSZ, MB_XL, MB_XR    # noqa: E402

S = W.S


# ---- the shipped binary's map. dispapps._map wants apps/<app>/<app>.asm and
# the package's shim is byteoply.asm, so it is mapped here and handed over --
def bp_map():
    with tempfile.TemporaryDirectory(prefix='bpmap-') as td:
        src = Path(td) / 'm.asm'
        mp = Path(td) / 'm.map'
        src.write_text((ROOT / 'apps/byteopoly/byteoply.asm').read_text()
                       + f'\n[map all {mp}]\n')
        subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', str(ROOT / 'apps') + '/',
                        '-I', str(ROOT / 'apps/byteopoly') + '/', '-I', str(ROOT / 'build') + '/',
                        '-o', str(Path(td) / 'm.bin'), str(src)], check=True, cwd=ROOT)
        out = {}
        for line in mp.read_text().splitlines():
            p = line.split()
            if len(p) == 3:
                try:
                    out[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
    out['os88_image_end'] = out['cc_image_end']     # (wireball.Machine's bss base)
    return out


MAP = bp_map()
dispapps._MAPS[('byteopoly', ())] = MAP


# ---- record layouts as SmallerC lays them out: a byte at 1, everything else
# at 2. Field order is bp.h's (BPState) and bp_link.c's (BPLink). The guards
# below hold the arithmetic to the map, so a moved field fails HERE --------
def layout(fields):
    off, out = 0, {}
    for name, size, align in fields:
        off = (off + align - 1) & ~(align - 1)
        out[name] = off
        off += size
    return out, (off + 1) & ~1


def b(*names):
    return [(n, 1, 1) for n in names]


def w(*names):
    return [(n, 2, 2) for n in names]


U32, SETUP, CMD, CKPT = (4, 2), (47, 1), (22, 2), (26, 2)
GAME, _ = layout(b('status', 'rules', 'rounds_limit', 'nseats') + [
    ('match_id', *U32), ('rev', *U32)] + w('turn', 'round') + b(
    'round_left', 'phase', 'active', 'decider') + [
    ('name', 36, 1), ('token', 4, 1), ('machine', 4, 1), ('alive', 4, 1), ('pos', 4, 1)])
LINK, LINK_SIZE = layout(
    b('on', 'ph', 'intent', 'bad', 'left', 'diverged', 'lst') + [('me', *U32), ('peer', *U32)]
    + w('clk', 'last', 'heard', 'sent', 'beat', 'msg', 'ckmsg')
    + [('lob', *SETUP), ('lin', *SETUP), ('want', *SETUP)] + w('lseq', 'linseq')
    + b('rmask', 'lhave', 'linmask', 'myready', 'nameout', 'nametries') + [('seed', *U32)]
    + b('pend', 'tries') + w('due') + [('rcmd', *CMD)] + b('rorigin', 'rd1', 'rd2', 'rcard', 'hwcode')
    + w('rseq', 'hseq', 'hw') + [('rrev', *U32), ('rdig', *U32)] + b('cpend') + w('cseq')
    + [('ccmd', *CMD), ('tcmd', *CMD)] + b('tok', 'torigin') + w('tseq') + [('dig', *U32)]
    + [('cout', 25, 1)] + b('cbusy', 'cnum', 'ctries', 'crx') + w('cdue', 'clast')
    + b('snap', 'shead', 'stries') + w('sn', 'sack', 'ssent', 'sdue') + [('srev', *U32), ('sdig', *U32)]
    + b('ck', 'ckwant', 'cktries', 'ckcode', 'pair', 'rsok', 'rscommit') + w('ckdue')
    + [('ckm', *CKPT), ('rsm', *CKPT)])
assert MAP['_bpl_scr'] - MAP['_bpl'] == LINK_SIZE, ('BPLink moved', MAP['_bpl_scr'] - MAP['_bpl'], LINK_SIZE)
assert MAP['_bp_mode'] - MAP['_bp_game'] == 238, 'BPState moved'

PH_MANAGE, PH_REPAIR, PH_BUY, PH_AUCTION, PH_CARD, PH_DEBT, PH_TRADE, PH_AFTER, PH_OVER = range(1, 10)
BPL_LOBBY, BPL_MATCH = 1, 2
PAIR_DONE, PAIR_FAIL = 2, 3
SU_TAB, SU_MACH, SU_READY, SU_LINK, SU_START = 0, 13, 17, 23, 24       # bp_setup.c's widget ids
GAME_MENU, PAIRED_SAVE, RESUME_PAIR, CLOSE = 1, 3, 4, 5                # bp_app.c's Game menu
F_INTENT, F_RESULT, F_ACK = 7, 9, 11
# bp_panels.c's keys, in the order tried: a greyed one only shows its reason,
# so the next candidate follows (AFTER: End Turn, or the bonus Roll doubles owe)
KEY = {PH_MANAGE: ['spc'], PH_REPAIR: ['r', 'p'], PH_BUY: ['b', 'a'], PH_AUCTION: ['p', 'b'],
       PH_CARD: ['ret'], PH_DEBT: ['s', 'k'], PH_TRADE: ['r', 'a'], PH_AFTER: ['e', 'spc']}


def u16(raw, i=0):
    return raw[i] | raw[i + 1] << 8


class Machine(W.Machine):
    def __init__(self, directory, index):
        super().__init__(directory, index, app='byteopoly', filename='BYTEOPLY.O88',
                         disk=W.artifact('byteopoly.img'), prefix='_')

    def mem(self, sym, off=0, n=1):
        return int.from_bytes(self.readseg(self.seg, self.sy[sym] + off, n), 'little')

    def link(self, field, n=1):
        return self.mem('_bpl', LINK[field], n)

    def game_at(self, field, n=1):
        return self.mem('_bp_game', GAME[field], n)

    def rev(self):
        return self.game_at('rev', 4)

    def dig(self):
        return self.link('dig', 4)

    def quiet(self):
        return (self.link('clk', 2) - self.link('heard', 2)) & 0xFFFF

    def key(self, name):
        W.Qemu.key(self, name)
        Q.ui_done(self, S, cap=0.5)

    def wait_for(self, cond, what, secs=20):
        assert Q.acted(self, cond, secs=secs, what=what), (what, self.state())

    def state(self):
        try:
            return dict(ph=self.link('ph'), rev=self.rev(), dig=hex(self.dig()), pend=self.link('pend'),
                        cpend=self.link('cpend'), tries=self.link('tries'), div=self.link('diverged'),
                        snap=self.link('snap'), quiet=self.quiet(), phase=self.game_at('phase'),
                        decider=self.game_at('decider'), pair=self.link('pair'),
                        crc=self.mem('_bp_diag', 0, 2), frame=self.mem('_bp_diag', 4, 2))
        except Exception as e:                              # noqa: BLE001
            return f'unreadable: {e}'

    # -- the setup screen: focus read back from the guest ------------------
    def focus(self, wid):
        for _ in range(40):
            if self.mem('_su_foc') == wid:
                return
            self.key('tab')
        raise AssertionError(f'setup focus never reached {wid}: it is {self.mem("_su_foc")}')

    def press(self, wid):
        self.focus(wid)
        self.key('ret')

    # -- a menu pick off menu_bar, confirmed before the release ------------
    def menu_pick(self, cell, item):
        raw = self.read(S('menu_bar'), (cell + 1) * MB_ENTSZ)
        x = (u16(raw, cell * MB_ENTSZ + MB_XL) + u16(raw, cell * MB_ENTSZ + MB_XR)) // 2
        Q.mouse_to(self, S, self.hmp, x, MBAR_H // 2)
        self.hmp('mouse_button 1')
        try:
            assert Q.acted(self, lambda: u16(self.read(S('menu_y1'), 2))
                           and self.read(S('menu_dropd'), 1)[0], secs=4, what='menu drop'), 'menu never dropped'
            assert self.read(S('menu_cell'), 1)[0] == cell, 'a different menu dropped'
            y1 = u16(self.read(S('menu_y1'), 2))
            ix = u16(self.read(S('menu_x1'), 2)) + 8
            Q.mouse_to(self, S, self.hmp, ix, y1 + 1 + item * MENU_ITEM_H + MENU_ITEM_H // 2, btn=1)
            assert Q.acted(self, lambda: u16(self.read(S('menu_sel'), 2)) == item, secs=4,
                           what='item highlight'), f'item {item} never highlighted (disabled?)'
        finally:
            self.hmp('mouse_button 0')
        Q.ui_done(self, S, cap=1.0)


class FaultRelay:
    """Frame-aware, game-blind: drop/duplicate/corrupt/delay whole frames."""
    def __init__(self):
        self.buf = [bytearray(), bytearray()]
        self.rules = []                 # [direction, type or None, action, count]
        self.held = [[], []]
        self.cut = False
        self.hits = []

    def add(self, direction, ftype, action, count=1):
        self.rules.append([direction, ftype, action, count])

    def __call__(self, direction, data):
        out = bytearray()
        for byte in data:
            if byte != 0x7E:
                self.buf[direction].append(byte)
                continue
            raw = bytes(self.buf[direction])
            self.buf[direction].clear()
            if not raw:
                continue
            wire = b'\x7e' + raw + b'\x7e'
            body = self.unescape(raw)
            ok = len(body) >= 3 and binascii.crc_hqx(body[:-2], 0xFFFF) == int.from_bytes(body[-2:], 'little')
            ftype = body[0] if ok else None
            if self.cut:
                continue
            action = None
            for r in self.rules:
                if r[0] == direction and r[3] > 0 and ok and (r[1] is None or r[1] == ftype):
                    r[3] -= 1
                    action = r[2]
                    self.hits.append((direction, ftype, action))
                    break
            if action == 'drop':
                continue
            if action == 'delay':
                self.held[direction].append(wire)
                continue
            if action == 'corrupt':
                bad = bytearray(wire)
                i = 2                                   # inside the body, never a 7E/7D
                bad[i] = bad[i] ^ 0x01 if bad[i] ^ 0x01 not in (0x7E, 0x7D) else bad[i] ^ 0x02
                out.extend(bad)
                continue
            out.extend(wire)
            if action == 'dup':
                out.extend(wire)
            for h in self.held[direction]:              # a late copy, after a newer frame
                out.extend(h)
            self.held[direction].clear()
        return bytes(out)

    @staticmethod
    def unescape(raw):
        out, esc = bytearray(), False
        for c in raw:
            if esc:
                out.append(c ^ 0x20)
                esc = False
            elif c == 0x7D:
                esc = True
            else:
                out.append(c)
        return bytes(out)


def in_sync(host, join):
    return (host.rev() == join.rev() and host.dig() == join.dig()
            and not host.link('pend') and not join.link('cpend'))


def open_link(host, join):
    host.press(SU_TAB + 1)              # Host Cable
    join.press(SU_TAB + 2)              # Join Cable
    host.press(SU_LINK)                 # Open Link (Auto)
    join.press(SU_LINK)
    for m in (host, join):
        m.wait_for(lambda m=m: m.link('ph') == BPL_LOBBY, 'both reach the lobby (US-16)', secs=40)


def lobby_start(host, join):
    lob = LINK['lob']
    n = host.link('lob')
    mach = [host.mem('_bpl', lob + 43 + i) for i in range(n)]
    if 1 not in mach:                   # a seat for the other PC (the Computer column)
        host.focus(SU_MACH + 1)
        host.key('right')
    host.wait_for(lambda: join.link('lseq', 2) == host.link('lseq', 2), 'the client has the roster')
    join.press(SU_READY)
    host.press(SU_READY)
    host.wait_for(lambda: host.link('rmask') == 3, 'both ready on one lobby revision')
    host.press(SU_START)
    for m in (host, join):
        m.wait_for(lambda m=m: m.link('ph') == BPL_MATCH, 'both start', secs=20)
    host.wait_for(lambda: in_sync(host, join), 'START applied on both, same digest')


def move(host, join, what):
    """One decision, keyed on the decider's own machine; both reach rev + 1."""
    r0 = host.rev()
    phase, decider = host.game_at('phase'), host.game_at('decider')
    m = (host, join)[host.mem('_bp_game', GAME['machine'] + decider)]
    keys = KEY.get(phase, []) + ['ret']
    for attempt in range(3 * len(keys)):
        k = keys[attempt % len(keys)]
        m.key(k)
        if k == 'k':
            # Bankrupt asks; a SECOND K, pressed at least 20 guest ticks after
            # the first one's handling, is Yes (SPEC 131.9.7: a confirmation
            # defaults to No and ignores a held or queued key)
            Q.pace(m, 1.5)
            m.key('k')
        if Q.acted(m, lambda: host.rev() != r0 or m.link('cpend'), secs=2, what='decision taken'):
            break
    host.wait_for(lambda: in_sync(host, join) and host.rev() == r0 + 1, what, secs=30)


def reopen(m):
    m.menu_pick(GAME_MENU, CLOSE)
    # bp_app.c asks "Leave the cable game?" (Yes is the default button)
    if not Q.acted(m, lambda: dispapps.pkg_seg(m, 0) is None, secs=2, what='closed without asking'):
        m.key('ret')
    assert Q.acted(m, lambda: dispapps.pkg_seg(m, 0) is None, secs=8, what='closed'), 'the package did not close'
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    dispcp.open_named(m, m.mo, S, W.settle, wx, wy, 'BYTEOPLY.O88')
    assert Q.acted(m, lambda: dispapps.pkg_seg(m, 0) is not None, secs=8, what='reopened'), 'no reopen'
    m.seg = dispapps.pkg_seg(m, 0)[1]


def main():
    with tempfile.TemporaryDirectory(prefix='bpl-', dir=ROOT / 'build') as td:
        machines, cable = [], None
        try:
            machines = [Machine(Path(td), 0), Machine(Path(td), 1)]
            host, join = machines
            cable = W.Cable(machines)
            relay = FaultRelay()
            cable.filter = relay        # direction 0: host -> client, 1: client -> host
            open_link(host, join)
            lobby_start(host, join)
            for _ in range(6):
                move(host, join, 'opening moves')
            # US-17/US-18: each fault, then the next decision must land once
            for d, t, act in ((0, F_RESULT, 'drop'), (1, F_ACK, 'drop'), (1, F_INTENT, 'dup'),
                              (0, None, 'corrupt'), (0, F_RESULT, 'delay'), (1, F_INTENT, 'drop')):
                relay.add(d, t, act)
                move(host, join, f'{act} of type {t}')
            assert host.mem('_bp_diag', 0, 2) + join.mem('_bp_diag', 0, 2) >= 1, 'the corrupt frame was not counted'
            # a pulled cable: both pause, nothing is applied meanwhile
            r0 = host.rev()
            relay.cut = True
            host.wait_for(lambda: host.quiet() >= 128 and join.quiet() >= 128, 'both pause on silence', secs=40)
            assert host.rev() == r0 == join.rev(), 'a revision moved with the cable out'
            relay.cut = False
            host.wait_for(lambda: host.quiet() < 20 and join.quiet() < 20 and in_sync(host, join),
                          'the match resumes when the cable returns', secs=40)
            for _ in range(4):
                move(host, join, 'play after the pull')
            # US-19: paired save, close both, reopen, Resume Pair
            host.menu_pick(GAME_MENU, PAIRED_SAVE)
            for m in machines:
                m.wait_for(lambda m=m: m.link('pair') in (PAIR_DONE, PAIR_FAIL), 'paired save ends', secs=60)
                assert m.link('pair') == PAIR_DONE, ('paired save failed', m.state())
            saved = (host.rev(), host.dig())
            move(host, join, 'a move after the save (not in the checkpoint)')
            for m in machines:
                reopen(m)
            open_link(host, join)
            for m in machines:
                m.menu_pick(GAME_MENU, RESUME_PAIR)
            for m in machines:
                m.wait_for(lambda m=m: m.link('ph') == BPL_MATCH, 'resumed from the pair', secs=60)
            assert (host.rev(), host.dig()) == saved == (join.rev(), join.dig()), 'not the saved checkpoint'
            # play on, every revision compared on both machines (bounded: the
            # complete games are t_link's, on the host)
            for _ in range(60):
                if host.game_at('status') != 1:
                    break
                move(host, join, 'play on')
            for i, m in enumerate(machines):
                m.hmp('screendump "' + W.artifact(f'byteopoly-link-{i}.ppm') + '"')
            print(f'byteopoly_link: ok - lobby, start, faults {relay.hits}, pulled cable, paired save, '
                  f'reopen + Resume Pair, play to rev {host.rev()} (status {host.game_at("status")}), '
                  f'same digest at every revision')
        except BaseException:
            for i, m in enumerate(machines):
                print('byteopoly_link failure PC', i, m.state(), flush=True)
                try:
                    m.hmp('screendump "' + W.artifact(f'byteopoly-link-failure-{i}.ppm') + '"')
                except Exception:                               # noqa: BLE001
                    pass
            raise
        finally:
            if cable:
                cable.close()
            for m in machines:
                m.cleanup()


if __name__ == '__main__':
    main()
