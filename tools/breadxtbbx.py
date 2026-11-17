#!/usr/bin/env python3
"""Inspect or convert Breadboard XT BBX1 documents (SPEC.md130.5).

This is an independent bounded wire-format reader, not an electrical solver.
The native application's bb_validate remains the installed loader authority.
The JSON form exposes the complete source document; it contains no cached nets.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

HEADER = 48
MAX_PARTS = 64
MAX_WIRES = 128
MAX_SIZE = HEADER + 16 * (MAX_PARTS + MAX_WIRES)
NONE = 65535
TYPES = {1: 'Resistor', 2: 'LED', 3: 'Switch', 4: 'Supply', 5: 'Ground',
         6: 'Pot', 7: 'Input', 8: 'Indicator', 9: 'Clock', 10: '74HC00',
         11: '74HC04', 12: '74HC08', 13: '74HC32', 14: '74HC86',
         15: 'Capacitor', 16: 'NE555'}
META = ('next_id', 'probe_a', 'probe_b', 'pan_column', 'pan_row', 'zoom',
        'step_100us', 'scope_div_100us', 'trigger', 'board_profile',
        'probe_kind_a', 'probe_kind_b', 'probe_id_a', 'probe_id_b',
        'probe_pin_a', 'probe_pin_b')
DEFAULT = dict(zip(META, (1, NONE, NONE, 0, 0, 1, 10, 5000, 0, 1,
                          0, 0, 0, 0, 0, 0)))


def word(v, label):
    if type(v) is not int or not 0 <= v <= 65535:
        raise ValueError(f'{label}: expected unsigned16 value')
    return v


def pin_count(typ):
    if 10 <= typ <= 14:
        return 14
    if typ == 16:
        return 8
    if typ == 6:
        return 3
    if typ in (1, 2, 3, 4, 15):
        return 2
    if typ in (5, 7, 8, 9):
        return 1
    raise ValueError(f'unsupported part type{typ}')


def pins(part):
    n = pin_count(part['type'])
    hp = part['pin']
    if n <= 3:
        result = hp[:n]
        if n > 1 and part['flags'] & 1:
            result[0], result[1] = result[1], result[0]
        return result
    anchor = hp[0]
    if not 210 <= anchor < 240:
        raise ValueError('DIP anchor must be a rowf hole')
    col = anchor - 210
    half = n // 2
    if col + half > 30:
        raise ValueError('DIP extends beyond column30')
    result = []
    for pin in range(1, n + 1):
        row, at = (7, pin - 1) if pin <= half else (6, n - pin)
        if part['flags'] & 1:
            row, at = 13 - row, half - 1 - at
        result.append(row * 30 + col + at)
    return result


def validate(doc):
    if doc.get('format') != 'BBX1':
        raise ValueError('format must be BBX1')
    if set(doc) != {'format', 'metadata', 'parts', 'wires'}:
        raise ValueError('unknown or missing top-level source fields')
    m = doc['metadata']
    if set(m) != set(META):
        raise ValueError('metadata fields do not match BBX1')
    for k, v in m.items():
        word(v, k)
    if m['board_profile'] != 1:
        raise ValueError('unsupported board profile')
    if not (m['next_id'] and m['pan_column'] < 30 and m['pan_row'] < 14
            and m['zoom'] in (1, 2) and 1 <= m['step_100us'] <= 1000
            and 10 <= m['scope_div_100us'] <= 60000 and m['trigger'] <= 2):
        raise ValueError('invalid view, timestep or trigger metadata')
    ps, ws = doc['parts'], doc['wires']
    if not isinstance(ps, list) or not isinstance(ws, list) or len(ps) > 64 or len(ws) > 128:
        raise ValueError('object count exceeds capacity')
    ids, occupied, pairs = {}, set(), set()

    def insert(h):
        if not 0 <= h < 420:
            raise ValueError(f'invalid hole{h}')
        if h in occupied:
            raise ValueError(f'hole{h} has more than one inserted lead')
        occupied.add(h)

    def stable(obj):
        ident = word(obj['id'], 'id')
        if not 0 < ident < m['next_id'] or ident == NONE or ident in ids:
            raise ValueError(f'duplicate or invalid stable ID{ident}')
        ids[ident] = obj

    for p in ps:
        if set(p) != {'id', 'type', 'pin', 'value', 'param', 'flags'}:
            raise ValueError('part record fields do not match BBX1')
        stable(p)
        typ = word(p['type'], 'type')
        n = pin_count(typ)
        for k in ('value', 'param', 'flags'):
            word(p[k], k)
        if not isinstance(p['pin'], list) or len(p['pin']) != 3:
            raise ValueError('part needs three stored pin words')
        for h in p['pin']:
            word(h, 'pin')
        used = 1 if n > 3 else n
        if any(x != NONE for x in p['pin'][used:]):
            raise ValueError('unused stored pins must be65535')
        allowed = 3 if typ == 3 else 1
        if p['flags'] & ~allowed:
            raise ValueError('unsupported part flags')
        v, a = p['value'], p['param']
        good = ((v >= 10 and (a <= 1000 if typ == 6 else a == 0)) if typ in (1, 6)
                else (4500 <= v <= 6000 and a == 0) if typ == 4
                else (1 <= v <= 60000 and a <= 6000) if typ == 15
                else (2 <= v <= 60000 and 1 <= a <= 999) if typ == 9
                else (v == 0 and a <= 3) if typ == 7
                else (v == 0 and a == 0))
        if not good:
            raise ValueError('component value/parameter out of range')
        for h in pins(p):
            insert(h)
    for w in ws:
        if set(w) != {'id', 'a', 'b', 'bend', 'color', 'nbend'}:
            raise ValueError('wire record fields do not match BBX1')
        stable(w)
        for k in ('a', 'b', 'color', 'nbend'):
            word(w[k], k)
        if w['a'] == w['b'] or w['color'] > 15 or w['nbend'] > 2:
            raise ValueError('invalid wire endpoints/color/bend count')
        if not isinstance(w['bend'], list) or len(w['bend']) != 4:
            raise ValueError('wire needs four bend words')
        for h in w['bend']:
            word(h, 'bend')
        if any(w['bend'][w['nbend'] * 2:]):
            raise ValueError('unused bend words must be zero')
        route = [(w['a'] % 30, w['a'] // 30)]
        route += [tuple(w['bend'][i:i+2]) for i in range(0, 2*w['nbend'], 2)]
        route += [(w['b'] % 30, w['b'] // 30)]
        for (x, y), (xx, yy) in zip(route, route[1:]):
            if not (0 <= xx < 30 and 0 <= yy < 14) or (x, y) == (xx, yy) or (x != xx and y != yy):
                raise ValueError('wire route must have nonzero orthogonal segments')
        pair = tuple(sorted((w['a'], w['b'])))
        if pair in pairs:
            raise ValueError('duplicate wire endpoints')
        pairs.add(pair)
        insert(w['a']); insert(w['b'])
    for ch in ('a', 'b'):
        kind, h, ident, pin = (m['probe_kind_'+ch], m['probe_'+ch],
                               m['probe_id_'+ch], m['probe_pin_'+ch])
        if kind == 0:
            if (h != NONE and h >= 420) or ident or pin:
                raise ValueError('invalid hole probe')
        else:
            target = next((p for p in ps if p['id'] == ident), None)
            if kind not in (1, 2) or h != NONE or target is None:
                raise ValueError('dangling part probe')
            if (not 1 <= pin <= pin_count(target['type'])) if kind == 1 else pin != 1:
                raise ValueError('invalid probe pin')
    return doc


def decode(data):
    if not HEADER <= len(data) <= MAX_SIZE or data[:4] != b'BBX1':
        raise ValueError('bad BBX1 magic or size')
    words = struct.unpack_from('<22H', data, 4)
    ver, size, np, nw = words[:4]
    if ver != 1:
        raise ValueError(f'unsupported BBX version{ver}')
    if size != HEADER or words[20:] != (0, 0) or len(data) != HEADER + 16*(np+nw):
        raise ValueError('invalid header length, reserved words or record count')
    m = dict(zip(META, words[4:20]))
    ps, ws, off = [], [], HEADER
    for _ in range(np):
        p = struct.unpack_from('<8H', data, off)
        ps.append(dict(id=p[0], type=p[1], pin=list(p[2:5]), value=p[5], param=p[6], flags=p[7]))
        off += 16
    for _ in range(nw):
        w = struct.unpack_from('<7H2B', data, off)
        ws.append(dict(id=w[0], a=w[1], b=w[2], bend=list(w[3:7]), color=w[7], nbend=w[8]))
        off += 16
    return validate(dict(format='BBX1', metadata=m, parts=ps, wires=ws))


def encode(doc):
    validate(doc)
    ps, ws, m = doc['parts'], doc['wires'], doc['metadata']
    data = bytearray(b'BBX1')
    data += struct.pack('<22H', 1, HEADER, len(ps), len(ws), *(m[k] for k in META), 0, 0)
    for p in ps:
        data += struct.pack('<8H', p['id'], p['type'], *p['pin'], p['value'], p['param'], p['flags'])
    for w in ws:
        data += struct.pack('<7H2B', w['id'], w['a'], w['b'], *w['bend'], w['color'], w['nbend'])
    return bytes(data)


def hole(h):
    if h == NONE:
        return '-'
    r, c = divmod(h, 30)
    row = ('top+', 'top-', *'abcdefghij', 'bottom+', 'bottom-')[r]
    return f'{row}{c+1}'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    for name in ('inspect', 'to-json', 'from-json'):
        s = sub.add_parser(name)
        s.add_argument('input', type=Path)
        if name != 'inspect':
            s.add_argument('-o', '--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'from-json':
        a.output.write_bytes(encode(json.loads(a.input.read_text())))
        print(f'{a.output}: BBX1 validated and written')
        return
    d = decode(a.input.read_bytes())
    if a.command == 'to-json':
        a.output.write_text(json.dumps(d, indent=2)+'\n')
        return
    print(f'{a.input}: BBX1 profile1,30columns, a-e/f-j strips, rail break15/16')
    print(f'{len(d["parts"])}parts, {len(d["wires"])}wires; source only, no cached solver results')
    print('View/probes:', json.dumps(d['metadata'], sort_keys=True))
    for v in d['parts']:
        print(f'  ID{v["id"]} {TYPES[v["type"]]} pins ' + ','.join(hole(h) for h in pins(v)) +
              f' value={v["value"]} parameter={v["param"]} flags={v["flags"]}')
    for v in d['wires']:
        print(f'  ID{v["id"]} wire {hole(v["a"])} -> {hole(v["b"])} bends={v["bend"][:2*v["nbend"]]} color={v["color"]}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, struct.error) as e:
        sys.exit(f'breadxtbbx: {e}')
