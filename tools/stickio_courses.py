#!/usr/bin/env python3
"""Version-one course authoring and bounded compiler checks (SPEC.md 103).
The P0 runtime supports a single dry room. Refuse future features explicitly.
"""
from pathlib import Path
import json
import re

MAX_WIDTH = 160
ROWS = 8
MAX_ACTORS = 20
NEAR_ACTORS = 6
ACTIVE_SPAN = 368
MATERIALS = ('air', 'solid', 'solid', 'solid', 'reward', 'hazard',
             'oneway', 'exit', 'checkpoint', 'oneway')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def encode_map(mp):
    raw = bytes(v for col in mp for v in col)
    result = bytearray()
    pos = 0
    while pos < len(raw):
        end = pos + 1
        while end < len(raw) and raw[end] == raw[pos] and end-pos < 255:
            end += 1
        result.extend((end-pos, raw[pos]))
        pos = end
    return bytes(result)


def decode_map(encoded, width):
    require(len(encoded) % 2 == 0, 'truncated RLE pair')
    raw = bytearray()
    for count, tile in zip(encoded[::2], encoded[1::2]):
        require(count > 0 and tile < len(MATERIALS), 'invalid RLE count/tile')
        raw.extend([tile]*count)
        require(len(raw) <= width*ROWS, 'RLE exceeds room')
    require(len(raw) == width*ROWS, 'incomplete RLE coverage')
    return bytes(raw)


def supported(mp, x, y):
    width = len(mp)
    if not (0 <= x <= width*16-16 and 0 <= y and y+24 < 128):
        return False
    for px in (x+3, x+12):
        for py in range(y, y+24):
            if mp[px//16][py//16] in (1, 2, 3, 5, 6):
                return False
    feet = y+24
    return feet % 16 == 0 and all(mp[px//16][feet//16] in (1, 2, 3, 9, 6)
                                 for px in (x+3, x+12))


def validate_course(level):
    width, mp = level['width'], level['map']
    require(type(width) is int and 20 <= width <= MAX_WIDTH, 'room width out of range')
    require(len(mp) == width and all(len(col) == ROWS for col in mp), 'map dimensions')
    require(all(type(v) is int and 0 <= v < len(MATERIALS) for col in mp for v in col),
            'illegal tile/material')
    require(type(level['world']) is int and 0 <= level['world'] < 6, 'world out of range')
    require(level.get('music', level['world']) == level['world'], 'unsupported music override')
    name = level['name']
    require(isinstance(name, str) and 0 < len(name) <= 24 and
            all(c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789' for c in name), 'illegal course name')
    ids = set()
    enemies = level['enemies']
    require(len(enemies) <= MAX_ACTORS, 'actor pool exceeded')
    kinds = {1: 'walker', 2: 'hopper', 3: 'flyer'}
    objects = level.get('objects', [dict(id='actor-%d'%i, x=e[0], y=e[1],
                                       kind=kinds.get(e[2]), direction=e[3])
                                  for i, e in enumerate(enemies)])
    require(len(objects) == len(enemies), 'object manifest does not match actors')
    for obj, (x, y, kind, direction) in zip(objects, enemies):
        require(isinstance(obj['id'], str) and obj['id'] and obj['id'] not in ids, 'duplicate/empty object ID')
        ids.add(obj['id'])
        require(all(type(v) is int for v in (x, y, kind, direction)) and
                0 <= x <= width*16-16 and 0 <= y <= 104 and
                kind in (1, 2, 3) and direction in (-1, 1), 'invalid actor spawn/type/direction')
        require((obj.get('x'), obj.get('y'), obj.get('kind'), obj.get('direction')) ==
                (x, y, kinds[kind], direction), 'object manifest disagrees with actor')
        require(kind == 3 or supported(mp, x, y), 'ground actor has unsafe initial support')
        require(kind != 3 or y+32 <= 104, 'flyer patrol outside room')
    positions = sorted(e[0] for e in enemies)
    require(all(sum(x <= other <= x+ACTIVE_SPAN for other in positions) <= NEAR_ACTORS
                for x in positions), 'activation/render budget exceeded')
    for label in ('spawn', 'checkpoint'):
        point = level[label]
        require(len(point) == 2 and all(type(v) is int for v in point), 'invalid '+label)
        x, y = point
        require(supported(mp, x, y), 'unsupported '+label)
        for col in range(max(0, (x-32)//16), min(width, (x+48+15)//16)):
            for row in range(y//16, (y+23)//16+1):
                require(mp[col][row] in (0, 7, 8), 'unprotected '+label)
            require(mp[col][(y+24)//16] in (1, 2, 3, 9), 'unsafe '+label+' arrival support')
        require(all(abs(ex-x) >= 64 for ex, *_ in enemies), 'actor in '+label+' arrival zone')
    exit_x = level['exit'][0]
    require(0 <= exit_x < width and level['exit'][1] in range(ROWS) and
            mp[exit_x][level['exit'][1]] == 7, 'missing exit trigger')
    require(supported(mp, exit_x*16, level['exit'][1]*16-8), 'unsupported exit')
    cp_x, cp_y = level['checkpoint']
    require(mp[cp_x//16][(cp_y+16)//16] == 8, 'checkpoint trigger disagrees with arrival')
    require(sum(v == 7 for col in mp for v in col) == 1 and
            sum(v == 8 for col in mp for v in col) == 1, 'ambiguous exit/checkpoint')
    rewards = [dict(id='tile-%d'%(x*ROWS+y), tile_index=x*ROWS+y, tile=v)
               for x, col in enumerate(mp) for y, v in enumerate(col) if v in (3, 4, 8)]
    for reward in rewards:
        require(reward['id'] not in ids, 'duplicate reward/object ID')
        ids.add(reward['id'])
    level['rewards'] = rewards
    level['objects'] = objects
    require(decode_map(encode_map(mp), width) == bytes(v for col in mp for v in col), 'RLE mismatch')
    return level


def authored_course(path):
    doc = json.loads(Path(path).read_text())
    require(doc.get('version') == 1, 'unsupported course version')
    require(set(doc) <= {'version', 'id', 'name', 'world', 'environment', 'music',
                         'rooms', 'checkpoint_policy'}, 'unsupported course field')
    require(doc.get('checkpoint_policy') == 'safe-retry-consumed-rewards', 'unsupported checkpoint policy')
    require(doc.get('environment') in ('surface', 'underground', 'elevated', 'foundry'), 'unsupported environment')
    require(isinstance(doc.get('id'), str) and
            re.fullmatch(r'[a-z0-9][a-z0-9-]{0,31}', doc['id']), 'invalid course ID')
    require(len(doc['rooms']) == 1, 'P0 supports one room')
    room = doc['rooms'][0]
    require(set(room) <= {'id', 'width', 'height', 'spawn', 'checkpoint', 'exit',
                         'terrain', 'objects', 'links', 'sections'}, 'unsupported room field')
    require(room.get('id') == 'main' and room.get('height') == ROWS, 'unsupported room dimensions/ID')
    require(not room.get('links'), 'room links require later runtime')
    width = room['width']
    require(type(width) is int and 20 <= width <= MAX_WIDTH, 'room width out of range')
    mp = [[0]*ROWS for _ in range(width)]
    for rect in room['terrain']:
        require(set(rect) == {'x', 'y', 'width', 'height', 'tile'}, 'terrain rectangle fields')
        x, y, w, h, tile = (rect[k] for k in ('x', 'y', 'width', 'height', 'tile'))
        require(all(type(v) is int for v in (x, y, w, h, tile)) and w > 0 and h > 0 and
                0 <= x < x+w <= width and 0 <= y < y+h <= ROWS and 0 <= tile < len(MATERIALS),
                'terrain rectangle out of range')
        for col in range(x, x+w):
            for row in range(y, y+h):
                mp[col][row] = tile
    kinds = {'walker': 1, 'hopper': 2, 'flyer': 3}
    enemies = []
    for obj in room['objects']:
        require(set(obj) == {'id', 'kind', 'x', 'y', 'direction'}, 'unsupported object fields')
        require(obj['kind'] in kinds, 'unsupported object kind')
        enemies.append([obj['x'], obj['y'], kinds[obj['kind']], obj['direction']])
    require(room['exit'].get('kind') == 'flag' and set(room['exit']) == {'kind', 'tile'}, 'unsupported exit condition')
    sections = room.get('sections', [])
    require(all(set(section) == {'name', 'start', 'end'} and
                0 <= section['start'] < section['end'] <= width for section in sections), 'invalid section')
    return validate_course(dict(id=doc['id'], name=doc['name'], world=doc['world'],
        environment=doc['environment'], music=doc['music'], room='main', width=width,
        map=mp, spawn=room['spawn'], checkpoint=room['checkpoint'], exit=room['exit']['tile'],
        enemies=enemies, objects=room['objects'], sections=sections))


def preview_svg(level):
    # Collision layout only: no decoration or proprietary imagery.
    palette = ('white', '#333', '#555', '#999', '#ccc', '#111', '#777', '#444', '#aaa', '#ddd')
    rects = ['<rect x="%d" y="%d" width="16" height="16" fill="%s"/>'%(x*16, y*16, palette[v])
             for x, col in enumerate(level['map']) for y, v in enumerate(col) if v]
    rects += ['<circle cx="%d" cy="%d" r="6" fill="red"/>'%(e[0]+8, e[1]+12) for e in level['enemies']]
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d 128">%s</svg>\n'%(level['width']*16, ''.join(rects))
