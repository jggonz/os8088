#!/usr/bin/env python3
"""bpsave - an independent reader for Byteopoly save files (SPEC.md 131.7).

Written from SPEC.md 131.7.1-131.7.3 and the board and card tables of 131.2
(NOT by translating apps/byteopoly/bp_codec.c): the host harness
(hosttest/t_save.c) writes every file it builds to build/bpsave/ with a
manifest of the verdict the C codec gave, and this reader must agree with
each one - same error class, and for a rule failure the same rule number.

    python3 tools/bpsave.py FILE...                  decode and describe
    python3 tools/bpsave.py --manifest MANIFEST      agree with the C verdicts

Exit status 0 = every file decoded (or, with --manifest, every verdict
agreed), 1 = a refusal (or a disagreement), 2 = usage.
"""
import struct
import sys

OK, EINVALID, EFORMAT, EVERSION, ECRC, EMATCH = 0, 10, 16, 17, 18, 19
NAMES = {0: 'OK', 10: 'EINVALID', 16: 'EFORMAT', 17: 'EVERSION', 18: 'ECRC', 19: 'EMATCH'}
NOBODY = 0xFF
RULES_VER = 1
ENC_VER = 1
ENC_LEN = 233
HDR = 40
STATE_MAX = 320
SAVE_MAX = 512

# SPEC 131.2.1: id -> (kind, group-or-deck, prop, price, rent). kind: 0 boot,
# 1 computer, 2 utility, 3 event (group = deck), 4 charge (price = amount),
# 5 repair, 6 swap, 7 crash. Computer groups C H A I R P = 0..5.
_B = [
    (0, 0, NOBODY, 0, 0), (1, 0, 0, 100, 6), (3, 0, NOBODY, 0, 0), (1, 0, 1, 120, 8),
    (1, 0, 2, 140, 10), (2, 0, 3, 150, 0), (4, 0, NOBODY, 100, 0), (5, 0, NOBODY, 0, 0),
    (1, 1, 4, 160, 12), (1, 1, 5, 180, 14), (3, 1, NOBODY, 0, 0), (1, 1, 6, 200, 16),
    (1, 2, 7, 240, 20), (1, 2, 8, 260, 22), (6, 0, NOBODY, 0, 0), (1, 2, 9, 280, 24),
    (2, 0, 10, 150, 0), (1, 3, 11, 300, 26), (3, 0, NOBODY, 0, 0), (1, 3, 12, 320, 28),
    (1, 3, 13, 340, 30), (7, 0, NOBODY, 0, 0), (1, 4, 14, 360, 32), (1, 4, 15, 380, 34),
    (3, 1, NOBODY, 0, 0), (1, 5, 16, 400, 36), (1, 5, 17, 420, 40), (4, 0, NOBODY, 75, 0),
]
# SPEC 131.2.9: id -> (deck, effect, arg, amount). effect: 0 gain 1 pay 2 to
# 3 forward 4 back 5 repairs 6 detain 7 keep. amount is the credits of a gain
# or pay card, else 0.
_C = [
    (0, 0, 0, 100), (0, 0, 0, 50), (0, 0, 0, 25), (0, 2, 0, 0),
    (0, 2, 16, 0), (0, 3, 3, 0), (0, 7, 0, 0), (0, 1, 0, 25),
    (1, 1, 0, 100), (1, 1, 0, 50), (1, 1, 0, 35), (1, 5, 0, 0),
    (1, 6, 0, 0), (1, 4, 3, 0), (1, 0, 0, 50), (1, 7, 0, 0),
]
GROUP_SIZE = [3, 3, 3, 3, 2, 2]
PROPS = [i for i, b in enumerate(_B) if b[2] != NOBODY]   # prop index -> space
KEEP = {0: 6, 1: 15}                                       # deck -> its Recovery Disk card


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def digest32(data):
    s1 = s2 = 0
    for b in data:
        s1 = (s1 + b) % 255
        s2 = (s2 + s1) % 255
    return crc16(data) | ((s2 << 8 | s1) << 16)


def fingerprint():
    out = struct.pack('<H', RULES_VER)
    for kind, grp, prop, price, rent in _B:
        out += struct.pack('<BBBHH', kind, grp, prop, price, rent)
    for cid, (deck, eff, arg, amt) in enumerate(_C):
        out += struct.pack('<BBBBH', cid, deck, eff, arg, amt)
    return digest32(out)


class Refuse(Exception):
    def __init__(self, code, rule=0, why=''):
        Exception.__init__(self, why)
        self.code, self.rule, self.why = code, rule, why


def bad(rule, why):
    raise Refuse(EINVALID, rule, why)


class Cur:
    def __init__(self, b):
        self.b, self.i = b, 0

    def u8(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.i)[0]
        self.i += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.i)[0]
        self.i += 4
        return v

    def raw(self, n):
        v = bytes(self.b[self.i:self.i + n])
        self.i += n
        return v

    def list8(self, n):
        return list(self.raw(n))


def decode_state(p):
    """SPEC 131.7.1's table, in order. p is the 233-byte payload."""
    if not p:
        raise Refuse(EFORMAT, 0, 'empty payload')
    if p[0] != ENC_VER:
        raise Refuse(EVERSION, 0, 'encoding version %d' % p[0])
    if len(p) != ENC_LEN:
        raise Refuse(EFORMAT, 0, 'payload is %d bytes, not %d' % (len(p), ENC_LEN))
    c = Cur(p)
    c.u8()
    s = {}
    s['status'], s['rules'], s['rounds_limit'], s['nseats'] = c.u8(), c.u8(), c.u8(), c.u8()
    s['match_id'], s['rev'] = c.u32(), c.u32()
    s['turn'], s['round'] = c.u16(), c.u16()
    s['round_left'], s['phase'], s['active'], s['decider'] = c.u8(), c.u8(), c.u8(), c.u8()
    s['name'] = [c.raw(8) for _ in range(4)]
    for k in ('token', 'machine', 'alive', 'pos'):
        s[k] = c.list8(4)
    s['cash'] = [c.u32() for _ in range(4)]
    for k in ('detained', 'tries', 'disks'):
        s[k] = c.list8(4)
    for k in ('owner', 'level', 'mortgaged'):
        s[k] = c.list8(18)
    s['d1'], s['d2'], s['doubles'], s['rolled'], s['pending_move'] = (c.u8() for _ in range(5))
    s['deck'] = [c.list8(8), c.list8(8)]
    s['deck_n'], s['deck_top'] = c.list8(2), c.list8(2)
    s['card'], s['ret_phase'], s['depth'] = c.u8(), c.u8(), c.u8()
    s['rng'] = c.u32()
    s['auc_space'], s['auc_open'], s['auc_passed'], s['auc_turn'], s['auc_high'] = (c.u8() for _ in range(5))
    s['auc_bid'] = c.u32()
    s['debt_to'], s['debt_reason'] = c.u8(), c.u8()
    s['debt_amount'] = c.u32()
    s['offer_id'] = c.u16()
    s['offer_from'], s['offer_to'], s['offer_disks'], s['offer_payer'] = (c.u8() for _ in range(4))
    s['offer_give'], s['offer_get'], s['offer_cash'] = c.u32(), c.u32(), c.u32()
    s['score'] = [c.u32() for _ in range(4)]
    s['winners'] = c.u8()
    assert c.i == ENC_LEN
    return s


def validate(s):
    """SPEC 131.7.2, rule by rule. Raises Refuse(EINVALID, rule)."""
    n = s['nseats']
    phase = s['phase']
    # 1
    if s['status'] not in (1, 2):
        bad(1, 'status')
    if s['rules'] not in (0, 1):
        bad(1, 'rules')
    if not 2 <= n <= 4:
        bad(1, 'nseats')
    if s['rules'] == 0:
        if s['rounds_limit'] != 0:
            bad(1, 'classic has no round limit')
    else:
        if s['rounds_limit'] not in (10, 20, 30):
            bad(1, 'quick limit')
        if s['round'] > s['rounds_limit']:
            bad(1, 'round past the limit')
    # 2
    if not 1 <= phase <= 9:
        bad(2, 'phase')
    if (s['status'] == 2) != (phase == 9):
        bad(2, 'status/phase')
    seatmask = (1 << n) - 1
    alive = s['alive']
    # 3
    for i in range(4):
        if alive[i] > 1:
            bad(3, 'alive not boolean')
        if i >= n and alive[i]:
            bad(3, 'unused seat alive')
    amask = sum(1 << i for i in range(4) if alive[i])
    cnt = bin(amask).count('1')
    if cnt < (2 if s['status'] == 1 else 1):
        bad(3, 'too few alive')
    if s['active'] >= n or (s['decider'] >= n and not (s['status'] == 2 and s['decider'] == NOBODY)):
        bad(3, 'active/decider')
    if s['round_left'] & ~seatmask:
        bad(3, 'round_left')
    if s['status'] == 1:
        if not alive[s['active']] or not alive[s['decider']]:
            bad(3, 'active/decider dead')
        if s['round_left'] & ~amask:
            bad(3, 'round_left dead')
    # 4, 5
    for i in range(4):
        nm = s['name'][i]
        if i >= n:
            if (s['token'][i] or s['machine'][i] or s['pos'][i] or s['detained'][i] or s['tries'][i]
                    or s['disks'][i] or s['cash'][i] or s['score'][i] or any(nm)):
                bad(4, 'unused seat not zero')
            continue
        stem = nm.split(b'\0', 1)[0]
        if not 1 <= len(stem) <= 8 or any(not 0x20 <= ch <= 0x7E for ch in stem):
            bad(4, 'name')
        if any(nm[len(stem):]):
            bad(4, 'bytes after the NUL')
        if s['token'][i] >= 4 or s['token'][i] in s['token'][:i]:
            bad(4, 'token')
        if s['machine'][i] > 1:
            bad(4, 'machine')
        if s['pos'][i] >= 28:
            bad(5, 'pos')
        if s['detained'][i] > 1 or (s['detained'][i] and s['pos'][i] != 7):
            bad(5, 'detained')
        if s['tries'][i] > 3 or (s['tries'][i] and not s['detained'][i]):
            bad(5, 'tries')
        if s['disks'][i] & ~3:
            bad(5, 'disks')
        if s['cash'][i] >> 16 >= 0x4000:
            bad(5, 'cash cap')
        if not alive[i] and (s['cash'][i] or s['detained'][i] or s['tries'][i] or s['disks'][i]):
            bad(5, 'eliminated seat keeps assets')
    # 6, 7
    groups = {}
    for p in range(18):
        o, lv, mg = s['owner'][p], s['level'][p], s['mortgaged'][p]
        if o != NOBODY and (o >= n or not alive[o]):
            bad(6, 'owner')
        if lv > 5 or mg > 1:
            bad(6, 'level/mortgage range')
        if o == NOBODY and (lv or mg):
            bad(6, 'unowned with level or mortgage')
        kind, grp = _B[PROPS[p]][0], _B[PROPS[p]][1]
        if kind == 2:
            if lv:
                bad(7, 'utility level')
        else:
            groups.setdefault(grp, []).append(p)
    for grp, ps in groups.items():
        lv = [s['level'][p] for p in ps]
        if max(lv) > 0:
            owners = {s['owner'][p] for p in ps}
            if len(owners) != 1 or NOBODY in owners or any(s['mortgaged'][p] for p in ps):
                bad(7, 'levels on an incomplete or mortgaged collection')
            if max(lv) - min(lv) > 1:
                bad(7, 'uneven levels')
    # 8
    d1, d2 = s['d1'], s['d2']
    if d1 > 6 or d2 > 6 or (d1 == 0) != (d2 == 0):
        bad(8, 'dice')
    if s['rolled'] > 1 or (s['rolled'] and not d1) or s['doubles'] > 2:
        bad(8, 'rolled/doubles')
    if s['pending_move'] > 12 or (s['pending_move'] and (phase != 6 or not s['rolled'])):
        bad(8, 'pending_move')
    # 9
    held = [[i for i in range(4) if s['disks'][i] >> d & 1] for d in range(2)]
    for d in range(2):
        if len(held[d]) > 1:
            bad(9, 'two holders')
        k = s['deck_n'][d]
        if k > 8 or k + len(held[d]) != 8:
            bad(9, 'deck_n')
        if (s['deck_top'][d] >= k) and (k or s['deck_top'][d]):
            bad(9, 'deck_top')
        cyc = s['deck'][d][:k]
        if any(cd >> 3 != d for cd in cyc) or len(set(cyc)) != len(cyc):
            bad(9, 'deck contents')
        if held[d] and KEEP[d] in cyc:
            bad(9, 'held disk in the cycle')
    if s['card'] != NOBODY and s['card'] >= 16:
        bad(9, 'card')
    # 10-14
    act = s['active']
    if phase == 2 and not s['detained'][act]:
        bad(10, 'repair needs detention')
    if phase == 3:
        sp = _B[s['pos'][act]]
        if sp[0] not in (1, 2) or s['owner'][sp[2]] != NOBODY:
            bad(10, 'buy on a non-lot')
    if phase == 4:
        sp = _B[s['auc_space']] if s['auc_space'] < 28 else None
        if sp is None or sp[2] >= 18 or s['owner'][sp[2]] != NOBODY:
            bad(11, 'auction lot')
        op, pa = s['auc_open'], s['auc_passed']
        if op != amask or pa & ~op:
            bad(11, 'auction masks')
        t = s['auc_turn']
        if t >= 4 or not op >> t & 1 or pa >> t & 1 or t == s['auc_high']:
            bad(11, 'auction turn')
        hi = s['auc_high']
        if hi == NOBODY:
            if s['auc_bid']:
                bad(11, 'bid without a bidder')
        else:
            if hi >= 4 or not op >> hi & 1 or pa >> hi & 1:
                bad(11, 'high bidder')
            b = s['auc_bid']
            if b == 0 or b % 10 or b > s['cash'][hi]:
                bad(11, 'bid')
    if phase == 5:
        cd = s['card']
        if cd >= 16:
            bad(12, 'card')
        d = cd >> 3
        if cd not in s['deck'][d][:s['deck_n'][d]]:
            bad(12, 'shown card not in its cycle')
        deck, eff, arg, _ = _C[cd]
        dest = None
        if eff == 2:
            dest = arg
        elif eff == 3:
            dest = (s['pos'][act] + arg) % 28
        elif eff == 4:
            dest = (s['pos'][act] - arg) % 28
        if dest is not None and _B[dest][0] == 2 and not s['rolled']:
            bad(12, 'utility landing needs the dice')
    if phase == 6:
        if not s['debt_amount']:
            bad(13, 'debt amount')
        dt = s['debt_to']
        if dt != NOBODY and (dt >= n or not alive[dt] or dt == act):
            bad(13, 'creditor')
        if s['ret_phase'] not in (1, 2, 3, 5, 8):
            bad(13, 'return phase')
    if phase == 7:
        if s['ret_phase'] not in (1, 2, 8):
            bad(14, 'return phase')
        fr, to = s['offer_from'], s['offer_to']
        if fr != act or to >= n or to == fr or not alive[to]:
            bad(14, 'parties')
        od = s['offer_disks']
        if s['offer_payer'] > 1 or od & ~0x33:
            bad(14, 'payer/disks')
        if (od & 3) & ~s['disks'][fr] or ((od >> 4) & 3) & ~s['disks'][to]:
            bad(14, 'disks not held')
        if s['offer_give'] >> 18 or s['offer_get'] >> 18:
            bad(14, 'prop mask')
        for p in range(18):
            if s['offer_give'] >> p & 1 and s['owner'][p] != fr:
                bad(14, 'give not owned')
            if s['offer_get'] >> p & 1 and s['owner'][p] != to:
                bad(14, 'get not owned')
        if not (s['offer_give'] or s['offer_get'] or s['offer_cash'] or od):
            bad(14, 'empty offer')
    # 15
    if s['status'] == 1:
        if s['winners']:
            bad(15, 'winners in play')
    elif not s['winners'] or s['winners'] & ~seatmask:
        bad(15, 'winners')


def parse(buf):
    """-> (header dict, state dict). Raises Refuse. SPEC 131.7.3's order."""
    n = len(buf)
    if n < HDR + 2 or buf[:4] != b'BPSV':
        raise Refuse(EFORMAT, 0, 'not a BPSV file')
    if buf[4] != 1:
        raise Refuse(EVERSION, 0, 'file format %d' % buf[4])
    plen = struct.unpack_from('<H', buf, 38)[0]
    if plen > STATE_MAX or n != HDR + plen + 2:
        raise Refuse(EFORMAT, 0, 'length field %d vs file %d' % (plen, n))
    if crc16(buf[:n - 2]) != struct.unpack_from('<H', buf, n - 2)[0]:
        raise Refuse(ECRC, 0, 'bad CRC')
    role, state = buf[5], buf[6]
    if role > 2 or state not in (1, 2):
        raise Refuse(EFORMAT, 0, 'role/state')
    rv, fp, match, ckpt, rev, dig, host, client = struct.unpack_from('<HIIIIIII', buf, 8)
    if rv != RULES_VER:
        raise Refuse(EVERSION, 0, 'rules version %d' % rv)
    if fp != fingerprint():
        raise Refuse(EMATCH, 0, 'fingerprint %08x' % fp)
    payload = bytes(buf[HDR:HDR + plen])
    st = decode_state(payload)
    validate(st)
    if match != st['match_id'] or rev != st['rev'] or dig != digest32(payload):
        raise Refuse(EFORMAT, 0, 'header disagrees with its payload')
    mm = 0
    for i in range(st['nseats']):
        mm |= 1 << st['machine'][i]
    if mm != (3 if role else 1):
        raise Refuse(EMATCH, 0, 'seat/machine map does not fit the role')
    hdr = dict(role=role, state=state, match=match, ckpt=ckpt, rev=rev, digest=dig,
               host=host, client=client, plen=plen)
    return hdr, st


def describe(path):
    with open(path, 'rb') as f:
        buf = f.read()
    hdr, st = parse(buf)
    nm = [st['name'][i].split(b'\0')[0].decode('ascii') for i in range(st['nseats'])]
    print('%s: ok  role=%d state=%d match=%08x ckpt=%d rev=%d digest=%08x phase=%d seats=%s' % (
        path, hdr['role'], hdr['state'], hdr['match'], hdr['ckpt'], hdr['rev'], hdr['digest'],
        st['phase'], ','.join(nm)))


def run_manifest(path):
    import os
    base = os.path.dirname(path)
    bad_n = checked = 0
    for line in open(path):
        line = line.strip()
        if line.startswith('#fingerprint'):
            want = int(line.split()[1], 16)
            if want != fingerprint():
                print('FAIL fingerprint: C %08x, python %08x' % (want, fingerprint()))
                bad_n += 1
            checked += 1
            continue
        if not line or line.startswith('#'):
            continue
        name, expect, rule = line.split()
        expect, rule = int(expect), int(rule)
        with open(os.path.join(base, name + '.bps'), 'rb') as f:
            buf = f.read()
        try:
            parse(buf)
            got, grule = OK, 0
        except Refuse as r:
            got, grule = r.code, r.rule
        checked += 1
        if got != expect or (expect == EINVALID and grule != rule):
            print('FAIL %s: C says %s rule %d, python says %s rule %d' % (
                name, NAMES.get(expect, expect), rule, NAMES.get(got, got), grule))
            bad_n += 1
    print('bpsave: %d verdicts checked, %d disagreements' % (checked, bad_n))
    return 1 if bad_n else 0


def main(argv):
    if len(argv) == 3 and argv[1] == '--manifest':
        return run_manifest(argv[2])
    if len(argv) < 2 or argv[1].startswith('-'):
        print(__doc__)
        return 2
    rc = 0
    for p in argv[1:]:
        try:
            describe(p)
        except Refuse as r:
            print('%s: refused %s rule %d: %s' % (p, NAMES.get(r.code, r.code), r.rule, r.why))
            rc = 1
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
