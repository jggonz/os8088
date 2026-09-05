#!/usr/bin/env python3
"""Pac-Man's imported maze graph: every dot is reachable, every edge bounded.

This walks all legal player positions, not a scripted route through a few
corridors. It catches a swapped direction bit, bad tunnel edge or omitted
junction in the imported tables, independently of the guest renderer.
"""
from collections import deque
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'apps/pacman/assets.inc').read_text()


def data(name):
    body = source.split('pm_' + name + ':\n', 1)[1].split('\npm_', 1)[0]
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', body))


maze = data('maze_source')
hs, vs, table = data('htable'), data('vtable'), data('junctions')
assert len(maze) == 880
assert maze.count(1) == 256 and maze.count(2) == 4
assert len(table) == 100 and len(hs) == len(vs) == 10
seen = {(122, 164)}
queue = deque(seen)
while queue:
    x, y = queue.popleft()
    if y not in vs:
        mask = 3
    elif x not in hs:
        mask = 12
    else:
        mask = table[vs.index(y) * 10 + hs.index(x)]
    # The table values follow MONHND: up/down/left/right. MAZHND's
    # prose accidentally reverses its horizontal bit descriptions.
    for bit, dx, dy in ((1, 0, -2), (2, 0, 2), (4, -1, 0), (8, 1, 0)):
        if not mask & bit:
            continue
        nx, ny = x + dx, y + dy
        if nx == 47:
            nx = 200
        elif nx == 201:
            nx = 48
        assert 48 <= nx <= 200 and 44 <= ny <= 196, (x, y, nx, ny)
        if (nx, ny) not in seen:
            seen.add((nx, ny))
            queue.append((nx, ny))
for index, cell in enumerate(maze):
    if cell in (1, 2):
        row, col = divmod(index, 40)
        assert (46 + 4 * col, 36 + 8 * row) in seen, (row, col)
for name in ('pacdot', 'pacrgt', 'paclft', 'pactop', 'pacbot',
             'monsup', 'monsdn', 'monslf', 'monsrt', 'monsfl', 'monsey'):
    assert len(data(name)) == 10, name
assert set(data('tiles')) <= {0x00, 0x99, 0xCC, 0xFF}
print(f'pacman: all 260 dots reachable through {len(seen)} legal positions; '
      'tunnel bounds, sprites and monochrome-safe tiles pass')
