#!/usr/bin/env python3
"""arenagen - WireArena's host-built tables (docs/plans/MULTIPLAYER-FPS-PLAN.md).

Writes one NASM include, deterministic byte for byte. Its constants are
always defined; each DATA block is assembled only where the includer asks for
it (docs/plans/completed/WIREARENA-PARTS-PLAN.md: the primary and the renderer part
each take what they read):

  WA_TAB_SIN     wa_sin
  WA_TAB_ARENAS  every arena's tables and wa_arenas
  WA_TAB_POSE0   wa_pose0, the HOT keyframes (IDLE, RUN: the renderer part)
  WA_TAB_POSE1   wa_pose1, the rest (the pose part, apps/wirearena/wapose.asm)
  WA_TAB_KBND    wa_ktop and wa_krch (the renderer part: each keyframe's
                 bound, read before its joints)

The constants come first, under a guard of their own, so a second include
of the file adds only the data blocks the includer has asked for by then.

  wa_sin        257 words, a quarter wave of sin in Q14 (16384 = 1.0), sin(0..256);
                wa_sincos (apps/wirearena/wasin.inc) mirrors it to all 1024 angles
  wa_boxes      the arena's solid blocks, axis-aligned, in world units
  wa_room       the room's inner bounds
  wa_paths      each dummy's patrol: waypoints with the leg's heading and
                step count precomputed, so the guest needs no atan2
  wa_pose       the stick figure's keyframes, PRE-PROJECTED for 16 view
                facings: per frame, per facing, 11 joints of (lateral, height)
                as signed bytes in world units

World units are 1/64 m. Angles are 1024 to a turn, counter-clockwise from
+x, the map's y axis pointing "north". Heights are units above the floor
(docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md): a box is a 4-tuple, a 3 m solid
block, or a Box with a height and a kind (solid, a RAMP - kind FLIGHT, a
slope from the floor at its low face to its height at the face it rises
toward, HEIGHTS-PLAN 7.4.2 - or a lift),
and an arena part carries each box's height, kind and drawn faces after its
descriptor (WIREARENA-PARTS-PLAN 7.5, HEIGHTS-PLAN 7.2.1).

    python3 tools/arenagen.py build/watab.inc
    python3 tools/arenagen.py --arena N build/waaN.asm   (arena N as a part)
"""
import math
import os
import re
import struct
import sys

UNIT = 64.0                     # world units a metre
NFACE = 16                      # view facings a pose is projected for
SPEED = 8                       # a dummy's run, units a 30 Hz tick (~4.8 m/s)

# --- the arena: a 32 m room with blocks in it (x0, y0, x1, y1), units ------
ROOM = (0, 0, 2048, 2048)
WALL_H = 192                    # today's walls and blocks: 3 m
STEPUP = 24                     # the most a step may rise and still walk
                                # (0.375 m, HEIGHTS-PLAN 3.1)
FRAD = 8                        # a dummy's feet: the square its floor is
                                # read under (wa_figtick; WA_FRAD)
SOLID, FLIGHT, LIFT, LINE = 0, 1, 2, 3
# A LINE (WIREARENA-ARENAS-PLAN 2): a box drawn as the low outline its
# height makes - a line on the floor - that the game treats as a WALL to
# feet (wa_hat) and as nothing to shots and to sight: the edge of a walk
# with no drop modelled beyond it. On the wire to the renderer it is a SOLID
# with bit 7 of its kind byte set, which only the game reads
# COLOURS (wa_bcol, wrtab.inc's wa_attrc): 0 the room's blue, 1 an item's
# cyan, 9 a hedge's green, 10 a platform's red
COL_ROOM, COL_ITEM, COL_HEDGE, COL_PLAT = 0, 1, 9, 10
# a pickup's kind (WEAPONS-PLAN 2.4; their table is with the pickups, below)
PK_SHOTGUN, PK_CHAINGUN, PK_GRENADE, PK_ROCKET, PK_RAIL = 0, 1, 2, 3, 4
PK_HEALTH, PK_SHIELD = 5, 6
FACES = "SENW"                  # face order, as everywhere: S E N W


class Box(tuple):
    """A box: the 4-tuple every geometry routine reads, plus its height
    (units above the floor; a lift's REST height), its kind, its colour (0 a
    boundary, 1 an item - wa_bcol), and for a flight the face it rises
    TOWARD and its tread count, for a lift its raised height. ON: the box it
    STANDS on (HEDGES-PLAN 8) - the one just before it in the arena's list,
    both low solids, it strictly inside that one's footprint and taller: its
    faces rise from that box's top. The ground is the tallest box over a
    point, so everything that reads heights takes a stack as it is."""
    def __new__(cls, x0, y0, x1, y1, h=WALL_H, kind=SOLID, col=1, toward=None,
                treads=0, top=None, name="", on=None):
        b = tuple.__new__(cls, (x0, y0, x1, y1))
        b.h, b.kind, b.col, b.toward, b.treads = h, kind, col, toward, treads
        b.top, b.name, b.on = top, name, on
        return b


def bx(b):
    """Any box as a Box: a bare 4-tuple is a 3 m solid item."""
    return b if isinstance(b, Box) else Box(*b)


def jump_gap():
    """The widest gap between two tops of one height that is a SAFE jump,
    units: half the ground a running jump covers (the game's own speed,
    jump and gravity, off waconst.inc) - the other half a margin for a
    take-off short of the edge, or off the square"""
    src = open(__file__.replace("tools/arenagen.py", "apps/wirearena/waconst.inc")
               if __file__.endswith("tools/arenagen.py") else
               "apps/wirearena/waconst.inc").read()
    k = lambda name: int(re.search(name + r"\s+equ\s+(\d+)", src).group(1))
    v, g, z, t = k("WA_JUMPV"), k("WA_GRAV"), 0, 0
    while True:
        z += v
        v -= g
        t += 1
        if z <= 0:
            break
    return t * k("WA_MSPD") // 8 // 2


def jump_apex():
    """The jump's highest point, units: the game's own integration
    (wa_tick: jz += jv, jv -= GRAV, 8.8 a tick) off its own constants."""
    src = open(__file__.replace("tools/arenagen.py", "apps/wirearena/waconst.inc")
               if __file__.endswith("tools/arenagen.py") else
               "apps/wirearena/waconst.inc").read()
    v = int(re.search(r"WA_JUMPV\s+equ\s+(\d+)", src).group(1))
    g = int(re.search(r"WA_GRAV\s+equ\s+(\d+)", src).group(1))
    z = top = 0
    while v > 0 or z > 0:
        z += v
        v -= g
        top = max(top, z)
        if z <= 0:
            break
    return top >> 8
BOXES = [
    (448, 448, 576, 576),       # four pillars round the middle
    (1472, 448, 1600, 576),
    (448, 1472, 576, 1600),
    (1472, 1472, 1600, 1600),
    (896, 960, 1152, 1088),     # a long block across the centre
    (192, 960, 256, 1088),      # stubs off the west and east walls
    (1792, 960, 1856, 1088),
    (960, 192, 1088, 320),      # and off the south and north ones
    (960, 1728, 1088, 1856),
]
# The open arena: the same room, only the four pillars - the sparser level
# that asks how much of a frame the scene's own complexity costs.
OPEN_BOXES = BOXES[:4]
# x, y, angle. NOT (1024, 256), where Wave 0 started: that is inside the
# south stub, so every step collided and WASD looked dead. arenas() checks.
PLAYER_START = (1024, 480, 256)  # facing north, the patrols crossing ahead

# The HEIGHTS demo (docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 4.1, revised for wave
# 3 in 7.4 from the owner's sketch): in the west an ANNEX, one plain block
# narrower than the room, with two straight hallways into it joined by a
# corridor along the room's west wall and, between the hallways, the back
# room - an alcove off that corridor - holding the jump platform; in the
# centre the platform with a RAMP up its south and north sides; in the east
# the tower with ONE lift, narrower than it, on its north side; two pillars
# in the open area. 13 boxes.
HB = 384                        # the annex and the pillars: 6 m, taller
                                # than any eye (HEIGHTS-PLAN 7.1)
HEIGHTS_BOXES = [
    Box(0, 0, 640, 384, HB, col=0, name="annex fill, south"),
    Box(0, 1664, 640, 2048, HB, col=0, name="annex fill, north"),
    Box(576, 576, 640, 1472, HB, col=0, name="annex front"),
    Box(128, 576, 576, 832, HB, col=0, name="annex block, south"),
    Box(128, 1216, 576, 1472, HB, col=0, name="annex block, north"),
    Box(384, 960, 576, 1088, 48, name="jump platform"),
    Box(1088, 896, 1408, 1152, 80, name="platform"),
    Box(1184, 704, 1312, 896, 80, FLIGHT, toward="N", treads=4,
        name="south ramp"),
    Box(1184, 1152, 1312, 1344, 80, FLIGHT, toward="S", treads=4,
        name="north ramp"),
    Box(1856, 896, 2048, 1152, 160, name="tower"),
    Box(1920, 1152, 2048, 1280, 16, LIFT, top=160, name="lift"),
    Box(1472, 384, 1600, 512, HB, name="pillar"),
    Box(1472, 1536, 1600, 1664, HB, name="pillar"),
]
NOPATROL = set()
# Arena 2's own patrols (HEIGHTS-PLAN 7.4.5), carried in its part: one up
# the south ramp, across the platform and down the north one; one through
# the annex's hallways and corridor; one round the tower. Checked for every
# rise a walker meets (patrol_checks)
HEIGHTS_PATHS = [
    [(1248, 600), (1248, 1450), (1700, 1450), (1700, 600)],
    [(800, 480), (64, 480), (64, 1568), (800, 1568)],
    [(1000, 300), (1750, 300), (1750, 1750), (1000, 1750)],
]


# --- ROUTES (docs/plans/completed/WIREARENA-AI-PLAN.md 3): a graph of nodes and legs the
# dummies (the bots) walk, a branch picked at each node. A leg is walkable
# both ways (WALK), or one way: onto the lift at its rest (LIFT_ON: the
# walker waits at its start for the lift to be down), off the raised lift
# onto the tower (LIFT_UP: it waits on the lift, a rider, for the top), or a
# drop off a platform (DROP: any fall, no climb). Each figure has a HOME
# loop: the benches walk it, and a node's first leg out is its loop's next
# one, so a bench's dummies walk their old patrols exactly.
WALK, LIFT_ON, LIFT_UP, DROP = 0, 1, 2, 3
HOP = 4                         # (THREE-ARENAS-PLAN 5.2) a hop up onto a box
                                # a step cannot climb and the jump clears (its
                                # apex less 8): one way; on the wire a WALK -
                                # the bot's height follows the floor onto it
WA_MGN = 32                     # nodes an arena's graph may have
WA_MLIFT = 4                    # lifts an arena may have (wafb.inc's)
HEIGHTS_GRAPH = dict(
    nodes=[(1248, 600), (1248, 1024), (1248, 1450), (1700, 1450),
           (1700, 600),                                 # 0-4: over the ramps
           (800, 480), (64, 480), (64, 1024), (64, 1568),
           (800, 1568),                                 # 5-9: the annex
           (1000, 300), (1750, 300), (1750, 1750),
           (1000, 1750),                                # 10-13: round it all
           (1984, 1216),                                # 14: on the lift
           (1750, 1216),                                # 15: beside it
           (1952, 1024),                                # 16: the tower's top
           (1800, 1024)],                               # 17: under its face
    legs=[(0, 1), (1, 2), (2, 3), (3, 4), (4, 0),
          (5, 6), (6, 7), (7, 8), (8, 9), (9, 5),
          (10, 11), (11, 12), (12, 13), (13, 10),
          (5, 10), (9, 13), (0, 10), (4, 11), (3, 12), (2, 13), (0, 5),
          (3, 15), (15, 17), (17, 4),
          (15, 14, LIFT_ON), (14, 16, LIFT_UP), (16, 17, DROP)],
    homes=[[0, 1, 2, 3, 4], [5, 6, 7, 8, 9], [10, 11, 12, 13]])


# RESPAWN POINTS (AI-PLAN 5): x, y, facing (1024 to the turn), on the floor.
# A player respawns at the one farthest from the nearest live bot, a bot at
# the one farthest from the player, walking on from the route node nearest
HEIGHTS_RESPAWNS = [(1024, 480, 256), (250, 1024, 0), (1650, 1850, 768),
                    (1700, 200, 256), (800, 1024, 0)]
FLAT_RESPAWNS = [(1024, 480, 256), (1024, 1600, 768), (320, 1024, 0),
                 (1750, 1024, 512)]
ARENA_RESPAWNS = {"a2": HEIGHTS_RESPAWNS}    # (the hedge's and twin's: below)
WA_MRSP = 8


# A SPAWN'S FACING (the owner, 2026-11-15: "check the view isn't shoved
# into a wall"): a respawn point or the start may stand beside a wall, but
# its view must run FACE_CLEAR units before anything eye-high (FACE_WALL or
# taller, or the room's wall where it has one). One that does not - the
# hedge's, between its rings, were the worst - is turned, at build time, to
# whichever of 16 directions runs clear the longest (the authored angle
# breaking ties), so nothing on the guest asks.
FACE_CLEAR = 448                # 7 m
FACE_WALL = 96                  # a box this tall fills the eye's view
FACE_DIRS = 16


def clear_run(tag, boxes, x, y, a, cap=1024):
    """units from (x, y) along angle a (1024ths, 256 north) before a wall"""
    walls = [b for b in boxes if b.h >= FACE_WALL or b.kind == LINE]
    room = ARENA_ROOMH.get(tag, WALL_H) > 0
    t = a * 2 * math.pi / 1024
    cx, cy = math.cos(t), math.sin(t)
    d = 0
    while d < cap:
        d += 8
        px, py = x + d * cx, y + d * cy
        if room and not (ROOM[0] < px < ROOM[2] and ROOM[1] < py < ROOM[3]):
            return d
        if any(b[0] < px < b[2] and b[1] < py < b[3] for b in walls):
            return d
    return cap


def facing(tag, boxes, x, y, a):
    """a spawn's facing: a, or turned to the clearest view (above)"""
    boxes = [bx(b) for b in boxes]
    if clear_run(tag, boxes, x, y, a) >= FACE_CLEAR:
        return a
    step = 1024 // FACE_DIRS

    def off(c):
        return min((c - a) & 1023, (a - c) & 1023)
    return max(range(0, 1024, step),
               key=lambda c: (clear_run(tag, boxes, x, y, c), -off(c)))


def on_top(x, y, boxes, r):
    """(x, y) stands on the floor or a box's top, the square (x +- r) over
    the one height alone (THREE-ARENAS-PLAN 5.1: the Pit's spawns and flags):
    no box over it taller, none under part of it lower - and no LINE"""
    f = floor_at(x, y, boxes, r)
    if floor_at(x, y, boxes, 0) != f:
        return False
    for b in boxes:
        if b[0] < x + r and x - r < b[2] and b[1] < y + r and y - r < b[3]:
            if b.kind == LINE or b.h > f or (b.h < f and b.kind != FLIGHT):
                return False
    return True


def respawns(tag, boxes, nodes, grid):
    """An arena's respawn points: [(x, y, facing, node)] - refused in a wall
    or off the floor; the node is the nearest the point sees"""
    boxes = [bx(b) for b in boxes]
    out = []
    for r in ARENA_RESPAWNS.get(tag, FLAT_RESPAWNS):
        (x, y, a), side = r[:3], (r[3] if len(r) > 3 else 0)
        if not on_top(x, y, boxes, 24):     # (a platform's top will do:
            raise SystemExit("arenagen: arena %s: a respawn point at (%d, %d) "
                             "is not on free floor" % (tag, x, y))
        if not in_walk(tag, x, y, 24):
            raise SystemExit("arenagen: arena %s: a respawn point at (%d, %d) "
                             "is outside its floor lines" % (tag, x, y))
        d = sorted(range(len(nodes)), key=lambda k: math.hypot(
            nodes[k][0] - x, nodes[k][1] - y))
        seen = [k for k in d if sight_clear(grid, (x, y), nodes[k])]
        a = facing(tag, boxes, x, y, a)     # (not into a wall)
        out.append((x, y, a, (seen or d)[0], side))
    if not 1 <= len(out) <= WA_MRSP:
        raise SystemExit("arenagen: arena %s: %d respawn points - 1 to %d"
                         % (tag, len(out), WA_MRSP))
    return out


def graph_of(tag):
    """An arena's route graph: its own, or its patrols' loops as one"""
    if tag in ARENA_GRAPHS:
        return ARENA_GRAPHS[tag]
    nodes, legs, homes = [], [], []
    for p in ARENA_PATHS.get(tag, PATHS):
        b = len(nodes)
        nodes += list(p)
        legs += [(b + j, b + (j + 1) % len(p)) for j in range(len(p))]
        homes.append(list(range(b, b + len(p))))
    return dict(nodes=nodes, legs=legs + PATH_LINKS, homes=homes)


def graph_edges(tag, boxes):
    """-> (nodes, edges, home edges): the DIRECTED edges, each (a, b, kind,
    x, y, heading | climb, steps, rev), every node's own in its HOME loop's
    order first. Refused: a leg no walker can walk, a node with more than
    four ways out, a node some other cannot reach, a home loop not walkable"""
    boxes = [bx(b) for b in boxes]
    g = graph_of(tag)
    nodes = g["nodes"]
    if len(nodes) > WA_MGN:
        raise SystemExit("arenagen: arena %s: %d route nodes - at most %d"
                         % (tag, len(nodes), WA_MGN))
    top = hmax(boxes)
    lift = [b for b in boxes if b.kind == LIFT]
    raised = [Box(*b, h=b.top, kind=SOLID) if b.kind == LIFT else b
              for b in boxes]
    dirs = []
    for leg in g["legs"]:
        a, b_, kind = (leg + (WALK,))[:3]
        dirs.append((a, b_, kind))
        if kind == WALK:
            dirs.append((b_, a, kind))
    flat = all(b.h >= top and b.kind == SOLID for b in boxes)
    for (a, b_, kind) in dirs:
        (x0, y0), (x1, y1) = nodes[a], nodes[b_]
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) // SPEED))
        bs = raised if kind == LIFT_UP else boxes
        walls = [b for b in boxes if b.kind == LINE or       # (a tall box
                 (b.h >= top and not climbed(b, boxes)) or  # climbed is a
                 (b.kind == LIFT and kind == WALK)]         # floor: twin's)
        prev = None if flat else floor_at(x0, y0, bs, FRAD)
        for k in range(n + 1):
            x, y = x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n
            if inside(x, y, walls, 12):
                raise SystemExit("arenagen: arena %s: route leg %d -> %d runs "
                                 "into a wall near (%d, %d)" % (tag, a, b_, x, y))
            if not in_walk(tag, x, y, 12):
                raise SystemExit("arenagen: arena %s: route leg %d -> %d leaves "
                                 "the floor lines near (%d, %d)"
                                 % (tag, a, b_, x, y))
            if flat:
                continue
            f = floor_at(x, y, bs, FRAD)
            up = jump_apex() - 8 if kind == HOP else STEPUP
            if f - prev > up or (prev - f > STEPUP and kind != DROP):
                raise SystemExit("arenagen: arena %s: route leg %d -> %d steps "
                                 "%d at (%d, %d) - a walk takes %d"
                                 % (tag, a, b_, f - prev, x, y, STEPUP))
            prev = f
    if lift and any(k in (LIFT_ON, LIFT_UP) for _, _, k in dirs) is False:
        pass
    for k_ in (LIFT_ON, LIFT_UP):
        for (a, b_, kind) in dirs:
            if kind == k_ and not lift:
                raise SystemExit("arenagen: arena %s: a lift leg, no lift"
                                 % tag)
    # each node's ways out, its home loop's next first
    nxt = {}
    for loop in g["homes"]:
        for j, a in enumerate(loop):
            nxt[a] = loop[(j + 1) % len(loop)]
    order = []
    for a in range(len(nodes)):
        out = [d for d in dirs if d[0] == a]
        out.sort(key=lambda d: 0 if nxt.get(a) == d[1] else 1)
        if a in nxt and (not out or out[0][1] != nxt[a] or out[0][2] != WALK):
            raise SystemExit("arenagen: arena %s: home loop leg %d -> %d is "
                             "not a walk" % (tag, a, nxt[a]))
        if len(out) > 4:
            raise SystemExit("arenagen: arena %s: route node %d has %d ways "
                             "out - at most 4" % (tag, a, len(out)))
        order += out
    for a in range(len(nodes)):         # every node reaches every other
        seen, todo = {a}, [a]
        while todo:
            c = todo.pop()
            for (u, v, _) in order:
                if u == c and v not in seen:
                    seen.add(v)
                    todo.append(v)
        if len(seen) != len(nodes):
            raise SystemExit("arenagen: arena %s: route node %d reaches only "
                             "%d of %d" % (tag, a, len(seen), len(nodes)))
    edges = []
    for (a, b_, kind) in order:
        (x0, y0), (x1, y1) = nodes[a], nodes[b_]
        dx, dy = x1 - x0, y1 - y0
        steps = max(1, int(round(math.hypot(dx, dy) / SPEED)))
        bs = raised if kind == LIFT_UP else boxes
        up = any(floor_at(x0 + dx * k / steps, y0 + dy * k / steps, bs, FRAD)
                 for k in range(steps + 1))
        edges.append([a, b_, kind, x0, y0,
                      angle_of(dx, dy) | (0x8000 if up else 0), steps, 0xFF])
    for i, e in enumerate(edges):
        for j, f in enumerate(edges):
            if f[0] == e[1] and f[1] == e[0]:
                e[7] = j
    homes = []
    for loop in g["homes"]:
        homes.append(next(i for i, e in enumerate(edges)
                          if e[0] == loop[0] and e[1] == loop[1]))
    if len(homes) != len(PATHS):
        raise SystemExit("arenagen: arena %s: %d home loops - WA_NFIG is %d"
                         % (tag, len(homes), len(PATHS)))
    return nodes, edges, homes


SIGHT_G = 64                    # the sight grid's cell (AI-PLAN 4.1)


def sight_grid(tag, boxes):
    """The tall boxes as a bit a 64-unit cell, column-major: bit (y & 7) of
    byte x * 4 + y / 8 (wa_sight). Refused: a tall box off the grid"""
    boxes = [bx(b) for b in boxes]
    top = hmax(boxes)
    n = (ROOM[2] - ROOM[0]) // SIGHT_G
    out = bytearray(n * n // 8)
    for b in boxes:
        if b.h < top:
            continue
        if any(v % SIGHT_G for v in b):
            raise SystemExit("arenagen: arena %s: %s is off the sight grid "
                             "(%d units)" % (tag, b.name or str(tuple(b)),
                                             SIGHT_G))
        for x in range(b[0] // SIGHT_G, b[2] // SIGHT_G):
            for y in range(b[1] // SIGHT_G, b[3] // SIGHT_G):
                out[x * n // 8 + y // 8] |= 1 << (y & 7)
    return bytes(out)


def sight_clear(grid, p, q):
    """wa_sight's walk on the host: a 64-unit step at a time, x 16 fixed
    point, from p to q - True when no tall cell is entered"""
    dx, dy = int(q[0]) - int(p[0]), int(q[1]) - int(p[1])
    n = max(abs(dx), abs(dy)) // SIGHT_G + 1
    sx, sy = int(dx * 16 / n), int(dy * 16 / n)
    x, y = int(p[0]) * 16, int(p[1]) * 16
    for _ in range(n):
        x += sx
        y += sy
        c, r = x >> 10, y >> 10
        if grid[c * 4 + (r >> 3)] >> (r & 7) & 1:
            return False
    return True


def graph_block(tag, boxes):
    """The route graph as the arena's part carries it and the game copies it
    (wa_loadarena): nodes, edges, each figure's home edge; then the bots'
    tables (AI-PLAN 3.1, 4.1): the sight grid, the nearest node to each PVS
    cell, and for each node and goal the edge to take first -> bytes"""
    nodes, edges, homes = graph_edges(tag, boxes)
    out = bytearray([len(nodes), len(edges)] + homes)
    if len(out) & 1:
        out.append(0)
    outs = {a: [] for a in range(len(nodes))}
    for i, e in enumerate(edges):
        outs[e[0]].append(i)
    for a, (x, y) in enumerate(nodes):
        o = outs[a] + [0xFF] * (4 - len(outs[a]))
        out += struct.pack('<hh4B', x, y, *o)
    lifts = [b for b in map(bx, boxes) if b.kind == LIFT]
    for (a, b_, kind, x, y, head, steps, rev) in edges:
        k = WALK if kind == HOP else kind  # (a hop: a walk to the game)
                                        # a lift's leg: WHICH lift, bits 4-5
        if kind in (LIFT_ON, LIFT_UP):  # (ARENAS-PLAN 4.1) - the one under
            px, py = nodes[b_ if kind == LIFT_ON else a]    # its rider
            on = [i for i, l in enumerate(lifts)
                  if l[0] <= px <= l[2] and l[1] <= py <= l[3]]
            if len(on) != 1:
                raise SystemExit("arenagen: arena %s: lift leg %d -> %d: its "
                                 "rider's node is on no lift" % (tag, a, b_))
            k |= on[0] << 4
        out += struct.pack('<hhHH4B', x, y, head, steps, b_, k, rev, 0)
    grid = sight_grid(tag, boxes)
    out += grid
    g = 1 << PVS_SHIFT                  # the nearest node each cell's middle
    for cx in range(ROOM[0], ROOM[2], g):   # sees (or, seeing none, the
        for cy in range(ROOM[1], ROOM[3], g):   # nearest)
            c = (cx + g / 2, cy + g / 2)
            d = sorted(range(len(nodes)), key=lambda k: math.hypot(
                nodes[k][0] - c[0], nodes[k][1] - c[1]))
            seen = [k for k in d if sight_clear(grid, c, nodes[k])]
            out.append((seen or d)[0])
    nn = len(nodes)                     # next hop: Dijkstra on steps (a
    hop = [[0xFF] * nn for _ in range(nn)]  # lift's leg dearer), back from
                                        # each goal
    for goal in range(nn):
        dist = {goal: 0}
        todo = [goal]
        while todo:
            todo.sort(key=lambda k: -dist[k])
            v = todo.pop()
            for i, e in enumerate(edges):
                if e[1] == v:
                    u = e[0]
                    nd = dist[v] + e[6] + (60 if e[2] in (LIFT_ON, LIFT_UP)
                                           else 0)
                    if u not in dist or nd < dist[u]:
                        dist[u] = nd
                        hop[u][goal] = i
                        todo.append(u)
    for a in range(nn):
        out += bytes(hop[a])
    if len(out) & 1:
        out.append(0)
    rs = respawns(tag, boxes, nodes, grid)
    out += struct.pack('<H', len(rs))
    for (x, y, a, n, side) in rs:       # (its SIDE: 0 neither, 1 red, 2
        out += struct.pack('<hhHBB', x, y, a, n, side)  # blue - CTF-PLAN 3)
    # each pickup's route node, where a bot takes it (AI-PLAN 3.3): one
    # within WA_PKR (the player's reach) of it, or 0xFF - none walks there
    pk = pickups(tag, boxes)
    out += struct.pack('<H', len(pk))
    for (k, x, y, z, ch) in pk:
        near = [i for i, (nx, ny) in enumerate(nodes)
                if abs(nx - x) < 40 and abs(ny - y) < 40]
        out.append(near[0] if near else 0xFF)
    if len(out) & 1:
        out.append(0)
    bench = ARENA_BENCH[tag]            # its play bench's views (ARENAS-PLAN
    out += struct.pack('<H', len(bench))    # 4.2: x, y, angle, base pitch,
    for (mark, x, y, a, p, f) in bench:     # floor, HUD mark - wa_pbstart)
        bxs = list(map(bx, boxes))
        if inside(x, y, [b for b in bxs if b.kind == LINE or
                         (b.h >= hmax(bxs) and not climbed(b, bxs))], 24):
            raise SystemExit("arenagen: arena %s: bench view %s stands in a "
                             "wall" % (tag, mark))
        if not in_walk(tag, x, y, 24):
            raise SystemExit("arenagen: arena %s: bench view %s is outside "
                             "its floor lines" % (tag, mark))
        out += struct.pack('<hhHhhH', x, y, a << 6, p, f, ord(mark))
    fl = flag_bases(tag, boxes, nodes)  # THE FLAGS' BASES (CTF-PLAN 3): a
    out += struct.pack('<H', len(fl))   # count, 0 or 2 - red's, blue's -
    for (x, y, n, z) in fl:             # then each one's place, route node
        out += struct.pack('<hhBB', x, y, n, z >> 1)    # and height / 2
    hl = hill(tag, boxes, nodes)        # THE HILL (MODES-PLAN 4): a count, 0
    out += struct.pack('<H', len(hl))   # or 1, then its place, its floor and
    for (x, y, z, a, b) in hl:          # the route's two nodes a bot crosses
        out += struct.pack('<hhhBB', x, y, z, a, b)     # it between
    out += minimap(tag, boxes)          # THE MINIMAP last (the Tab screen)
    return bytes(out)


# THE MINIMAP (the Tab screen, WIREARENA-NET-PLAN 9.7): the arena's floor
# plan as MM_W x MM_H samples over the room, north (+y) at the top, each one
# of four kinds - 0 off the walk (beyond a floor line), 1 floor, 2 a low box
# or a floor up high (a top a player stands on: a platform, a lift, a
# climbed tower), 3 a wall (a box at the arena's tallest, or a LINE). Packed
# for the 80-column screen's cells: a cell is two samples, one over the
# other (its top half-block and its bottom), in a nibble - top in bits 0-1,
# bottom in 2-3 - and a byte is two cells side by side, the left in the low
# nibble. So a byte is also exactly one 40-column cell's two columns, which
# the game takes the larger kind of (a wall wins).
MM_W, MM_H = 32, 26                     # samples: 64 x ~79 units each
MM_KINDS = (0, 1, 2, 3)


def minimap(tag, boxes):
    """-> MM_H/2 rows of MM_W/2 bytes: the arena's floor plan, as above"""
    bxs = list(map(bx, boxes))
    top = hmax(bxs)

    def kind(b):
        if b.kind == LINE or (b.h >= top and not climbed(b, bxs)):
            return 3
        return 2
    sw = (ROOM[2] - ROOM[0]) / MM_W
    sh = (ROOM[3] - ROOM[1]) / MM_H
    grid = [[0] * MM_W for _ in range(MM_H)]
    for r in range(MM_H):                       # r 0 is the north edge
        y0 = ROOM[3] - (r + 1) * sh
        for c in range(MM_W):
            x0 = ROOM[0] + c * sw
            k = 1 if in_walk(tag, x0 + sw / 2, y0 + sh / 2) else 0
            for b in bxs:                       # a box marks a sample it
                ox = min(b[2], x0 + sw) - max(b[0], x0)     # covers half of
                oy = min(b[3], y0 + sh) - max(b[1], y0)     # - or half of
                if ox >= min(b[2] - b[0], sw) / 2 and \
                        oy >= min(b[3] - b[1], sh) / 2:      # the box, a thin
                    k = max(k, kind(b))                     # one
            grid[r][c] = k
    out = bytearray()
    for r in range(0, MM_H, 2):
        for c in range(0, MM_W, 2):
            lo = grid[r][c] | grid[r + 1][c] << 2
            hi = grid[r][c + 1] | grid[r + 1][c + 1] << 2
            out.append(lo | hi << 4)
    # THE PHASE at 40 columns, a byte after: a cell is two samples there, the
    # larger kind of the pair - so a 2 m corridor between two 1 m walls that
    # straddles a pair's boundary shows as wall (the crossing's north-south
    # arm, the owner's report). Pairs (2c, 2c+1), or (2c-1, 2c) - one sample
    # alone at either end, a cell more - whichever splits fewer walls from
    # floor; the first on a tie
    def split(ph):
        n = 0
        for row in grid:
            for c in range(-ph, MM_W, 2):
                pair = {row[k] for k in (c, c + 1) if 0 <= k < MM_W}
                n += 1 in pair and 3 in pair
        return n
    out.append(1 if split(1) < split(0) else 0)
    return bytes(out)


def minimap_text(tag, boxes):
    """the minimap as text, for a look on the host: . floor, : low, # wall"""
    m = minimap(tag, boxes)
    rows = []
    for r in range(MM_H):
        line = ""
        for c in range(MM_W):
            b = m[(r // 2) * (MM_W // 2) + c // 2]
            n = (b >> 4) if c & 1 else b & 15
            line += " .:#"[(n >> 2) & 3 if r & 1 else n & 3]
        rows.append(line)
    return "\n".join(rows)


# THE MODES an arena plays (WIREARENA-MODES-PLAN 1, 6), a bit each by the
# game's numbers (wa_gmode): free-for-all always; capture the flag where it
# has bases; team deathmatch where each side has two respawn points; king
# of the hill where it has a hill
MODE_FFA, MODE_CTF, MODE_TDM, MODE_KOTH = 0, 1, 2, 3


def arena_modes(tag):
    sides = [r[3] if len(r) > 3 else 0
             for r in ARENA_RESPAWNS.get(tag, FLAT_RESPAWNS)]
    m = 1 << MODE_FFA
    if ARENA_FLAGS.get(tag):
        m |= 1 << MODE_CTF
    if sides.count(1) >= 2 and sides.count(2) >= 2:
        m |= 1 << MODE_TDM
    if ARENA_HILL.get(tag):
        m |= 1 << MODE_KOTH
    return m


# THE HILL (MODES-PLAN 4): (x, y, node a, node b) - on the free floor or a
# platform, on the route between two nodes (or at one: a == b) so that a
# bot going from one to the other runs over it. The heights' central
# platform; the hedge's court at the stack's south foot (the stack itself
# has no route - a bot cannot climb it - so it is the high ground that
# overlooks the hill); the Twin's path at its bottom, between its lanes
ARENA_HILL = {"a2": (1248, 1024, 1, 1), "a3": (1024, 832, 1, 1),
              "a4": (1024, 832, 17, 18), "a5": (1024, 1024, 12, 12),
              "a6": (1024, 1024, 12, 12), "a7": (1024, 1024, 6, 6)}
WA_FLR = 40                     # a flag is touched within this of it, in x
                                # and in y (the game's WA_FLR; a pickup's 40)


def hill(tag, boxes, nodes):
    """An arena's hill: [(x, y, z, a, b)] or [] - refused off a floor, or
    out of reach of the route between its nodes"""
    if tag not in ARENA_HILL:
        return []
    x, y, a, b = ARENA_HILL[tag]
    boxes = [bx(bb) for bb in boxes]
    z = floor_at(x, y, boxes, 24)
    if inside(x, y, [bb for bb in boxes if bb.h > z], 24) or \
            not in_walk(tag, x, y, 24):
        raise SystemExit("arenagen: arena %s: the hill at (%d, %d) is not "
                         "on a floor" % (tag, x, y))
    (ax, ay), (bx_, by) = nodes[a], nodes[b]
    for t in range(17):                 # (its nearest on the leg a to b)
        px, py = ax + (bx_ - ax) * t / 16, ay + (by - ay) * t / 16
        if abs(px - x) < WA_FLR and abs(py - y) < WA_FLR:
            break
    else:
        raise SystemExit("arenagen: arena %s: the route from node %d to %d "
                         "passes nowhere within %d of the hill" % (tag, a, b,
                                                                  WA_FLR))
    return [(x, y, z, a, b)]


def flag_bases(tag, boxes, nodes):
    """An arena's flag bases (CTF-PLAN 3): [(x, y, node)], red then blue, or
    [] - refused off the free floor, without a route node within reach, or
    with either side short of two respawn points"""
    fl = ARENA_FLAGS.get(tag, [])
    if not fl:
        return []
    if len(fl) != 2:
        raise SystemExit("arenagen: arena %s: %d flag bases - CTF takes two"
                         % (tag, len(fl)))
    boxes = [bx(b) for b in boxes]
    out = []
    for (x, y) in fl:
        if not on_top(x, y, boxes, 24) or not in_walk(tag, x, y, 24):
            raise SystemExit("arenagen: arena %s: a flag base at (%d, %d) is "
                             "not on free floor" % (tag, x, y))
        z = floor_at(x, y, boxes, 24)
        if z & 1 or z > 510:            # (its record's byte: z / 2)
            raise SystemExit("arenagen: arena %s: a flag base %d up - its "
                             "byte holds an even height to 510" % (tag, z))
        near = [i for i, (nx, ny) in enumerate(nodes)
                if abs(nx - x) < WA_FLR and abs(ny - y) < WA_FLR]
        if not near:
            raise SystemExit("arenagen: arena %s: no route node within %d of "
                             "the flag base at (%d, %d) - a bot could not "
                             "take it" % (tag, WA_FLR, x, y))
        out.append((x, y, near[0], z))
    sides = [r[3] if len(r) > 3 else 0
             for r in ARENA_RESPAWNS.get(tag, FLAT_RESPAWNS)]
    for t in (1, 2):
        if sides.count(t) < 2:
            raise SystemExit("arenagen: arena %s has flags and %d respawn "
                             "points on side %d - a team mode wants two at "
                             "least" % (tag, sides.count(t), t))
    return out


# --- THE HEDGES' HEIGHTS: a hedge 6 m, over every eye (a tier's top and the
# outer wall's included); the outer wall's top walked round (HEDGES-PLAN 1).
# The first maze (WIREARENA-ARENAS-PLAN 1) was retired for the hedge, the
# owner's redrawing of it, 2026-11-15
MH = 384
MP = 128

# --- TWIN (ARENAS-PLAN 3, the owner's drawing of 2026-11-14): one of the
# LIGHT arenas by design - two squares of floor joined by a path that dips
# to the middle and back, drawn as FLOOR LINES alone (4.5; no room walls, 4.6; no
# boxes for them), and nothing above the floor but the two towers and the
# lift hidden behind each. A railgun on each tower, a chaingun beside each,
# health or shield where the path bottoms out; the flags of a CTF to come
# behind each lift (TWIN_FLAGS: CTF's bases, ARENA_FLAGS).
TT = 240                        # a tower: the tallest box, so it hides its
                                # lift from the path; the lift raised
                                # flush with its top
TWIN_WALK = [[                  # the walk's edge, one closed line: the west
    (704, 1088), (704, 1344), (64, 1344), (64, 704),        # square, the
    (704, 704), (704, 960),                                 # path's south
    (960, 768), (1088, 768), (1344, 960),                   # edge, the east
    (1344, 704), (1984, 704), (1984, 1344), (1344, 1344),   # square, the
    (1344, 1088), (1088, 896), (960, 896)]]                 # path's north
TWIN_BOXES = [
    Box(448, 896, 576, 1152, TT, col=COL_ITEM, name="west tower"),
    Box(320, 960, 448, 1088, 16, LIFT, col=COL_ITEM, top=TT,
        name="west tower's lift"),
    Box(1472, 896, 1600, 1152, TT, col=COL_ITEM, name="east tower"),
    Box(1600, 960, 1728, 1088, 16, LIFT, col=COL_ITEM, top=TT,
        name="east tower's lift")]
TWIN_FLAGS = [(192, 1024), (1856, 1024)]        # (CTF, the owner's red)
TWIN_GRAPH = dict(
    nodes=[(256, 800), (640, 800), (640, 1024), (640, 1248), (256, 1248),
           (256, 1024),                         # 0-5: round the west tower
           (384, 1024), (512, 1024),            # 6-7: its lift, its top
           (1792, 1248), (1408, 1248), (1408, 1024), (1408, 800),
           (1792, 800), (1792, 1024),           # 8-13: round the east one
           (1664, 1024), (1536, 1024),          # 14-15: its lift, its top
           (736, 1024), (960, 856), (1088, 856), (1312, 1024),
           (1088, 808), (960, 808),             # 16-21: the path, two ways
           (192, 1024), (1856, 1024)],          # 22-23: the flags' bases
    legs=[(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (5, 0),
          (8, 9), (9, 10), (10, 11), (11, 12), (12, 13), (13, 8),
          (16, 17), (17, 18), (18, 19), (19, 20), (20, 21), (21, 16),
          (2, 16), (19, 10),
          (5, 6, LIFT_ON), (6, 7, LIFT_UP), (7, 2, DROP),
          (13, 14, LIFT_ON), (14, 15, LIFT_UP), (15, 10, DROP),
          (5, 22), (13, 23)],                   # (spurs to the bases)
    homes=[[0, 1, 2, 3, 4, 5], [8, 9, 10, 11, 12, 13],
           [16, 17, 18, 19, 20, 21]])
TWIN_RESPAWNS = [(192, 768, 0, 1), (640, 1280, 0, 1), (192, 1280, 0, 1),
                 (1856, 1280, 512, 2), (1408, 768, 512, 2),
                 (1856, 768, 512, 2)]  # (the west square red, the east blue)
TWIN_PICKUPS = [
    (PK_RAIL, 512, 1024), (PK_RAIL, 1536, 1024),     # the towers' tops
    (PK_CHAINGUN, 512, 800), (PK_CHAINGUN, 1536, 1248),  # beside them
    ((PK_HEALTH, PK_SHIELD), 1024, 832),             # the path's bottom
]
TWIN_START = (640, 800, 0)


# --- THE HEDGE (docs/plans/completed/WIREARENA-HEDGES-PLAN.md): the owner's redrawing
# of the maze, 2026-11-14 - chosen over a second drawing with four gaps a
# ring for its play: the gaps' offsets tell you where you are. The same rings
# - a court with the red stack, an inner ring, an outer ring - but the court
# larger, every corridor 2 m (128) wide, every gap in a hedge 2 m and never
# facing the next one out, and past the outer ring a 1 m WALL (the outer
# wall's top, MP high) walked round the whole map, its gaps narrow enough to
# jump. The three rooms stand OUTSIDE that wall, west, east and south, each a
# 4 x 8 m pocket off a gap, a platform in it; the lift is in the north gap
# and rises to the wall's top. Tall fills round the rooms are the EDGE, in
# the room's blue as the room's own walls are (the owner's drawing): a tall
# box is the cheap one to draw and the one that hides.
# Absolute coordinates; the main square x 256..1792, y 256..2048, the
# court's middle (1024, 1152). The court's hedge is open N and S at the
# middle; the middle hedge W by its north end and E by its south end.
def _hedge():
    H, G = MH, COL_HEDGE
    STK0 = Box(832, 960, 1216, 1344, 48, col=COL_PLAT, name="stack bottom")
    bx_ = [
        # the stack, as the maze's - 384, 256 and 128 square - but three
        # boxes, each STANDING on the one under it (HEDGES-PLAN 8), not nine
        STK0,
        Box(896, 1024, 1152, 1280, 104, col=COL_PLAT, name="stack middle",
            on=STK0),
    ]
    bx_ += [
        Box(960, 1088, 1088, 1216, 160, col=COL_PLAT, name="stack top",
            on=bx_[-1]),
        # the court's hedge, x 640..1408, y 640..1664, 1 m thick: its gaps
        # x 960..1088, N and S
        Box(640, 640, 704, 1664, H, col=G, name="inner hedge W"),
        Box(1344, 640, 1408, 1664, H, col=G, name="inner hedge E"),
        Box(704, 1600, 960, 1664, H, col=G, name="inner hedge N-W"),
        Box(1088, 1600, 1344, 1664, H, col=G, name="inner hedge N-E"),
        Box(704, 640, 960, 704, H, col=G, name="inner hedge S-W"),
        Box(1088, 640, 1344, 704, H, col=G, name="inner hedge S-E"),
        # the middle hedge, x 448..1600, y 448..1856: a side a box, each past
        # a corner, so a gap is where one side stops short of the next - W
        # y 1664..1792, E y 512..640
        Box(448, 1792, 1600, 1856, H, col=G, name="middle hedge N"),
        Box(1536, 640, 1600, 1792, H, col=G, name="middle hedge E"),
        Box(512, 448, 1600, 512, H, col=G, name="middle hedge S"),
        Box(448, 448, 512, 1664, H, col=G, name="middle hedge W"),
        # the outer wall, 1 m thick and MP high, walked round: its gaps 2 m,
        # into the rooms and (north) the lift's
        Box(256, 256, 320, 1088, MP, col=G, name="wall W-S"),
        Box(256, 1216, 320, 2048, MP, col=G, name="wall W-N"),
        Box(1728, 256, 1792, 1088, MP, col=G, name="wall E-S"),
        Box(1728, 1216, 1792, 2048, MP, col=G, name="wall E-N"),
        Box(320, 256, 960, 320, MP, col=G, name="wall S-W"),
        Box(1088, 256, 1728, 320, MP, col=G, name="wall S-E"),
        Box(320, 1984, 960, 2048, MP, col=G, name="wall N-W"),
        Box(1088, 1984, 1728, 2048, MP, col=G, name="wall N-E"),
        Box(960, 1984, 1088, 2048, 16, col=COL_PLAT, kind=LIFT, top=MP,
            name="the lift"),
        # the edge: tall fills round the three rooms (W and E 4 x 8 m, y
        # 896..1408; S 8 x 4 m, x 768..1280), and each room's platform
        Box(0, 0, 256, 896, H, col=COL_ROOM, name="fill W-S"),
        Box(0, 1408, 256, 2048, H, col=COL_ROOM, name="fill W-N"),
        Box(1792, 0, 2048, 896, H, col=COL_ROOM, name="fill E-S"),
        Box(1792, 1408, 2048, 2048, H, col=COL_ROOM, name="fill E-N"),
        Box(256, 0, 768, 256, H, col=COL_ROOM, name="fill S-W"),
        Box(1280, 0, 1792, 256, H, col=COL_ROOM, name="fill S-E"),
        Box(64, 1088, 192, 1216, 48, col=COL_PLAT, name="west room's platform"),
        Box(1856, 1088, 1984, 1216, 48, col=COL_PLAT, name="east room's platform"),
        Box(960, 64, 1088, 192, 48, col=COL_PLAT, name="south room's platform")]
    # the route graph: the court round the stack, the inner ring, the outer
    # ring, the three rooms (their nodes before the platforms, as the maze's)
    court = [(768, 832), (1024, 832), (1280, 832), (1280, 1472),
             (1024, 1472), (768, 1472)]                             # 0-5
    inner = [(576, 576), (1024, 576), (1472, 576), (1472, 1728),
             (1024, 1728), (576, 1728)]                             # 6-11
    outer = [(384, 384), (1024, 384), (1664, 384), (1664, 576),
             (1664, 1152), (1664, 1920), (1024, 1920), (384, 1920),
             (384, 1728), (384, 1152)]                              # 12-21
    nodes = court + inner + outer + [(1824, 1152), (224, 1152), (1024, 224)]
    legs = [(i, (i + 1) % 6) for i in range(6)] + \
           [(6 + i, 6 + (i + 1) % 6) for i in range(6)] + \
           [(12 + i, 12 + (i + 1) % 10) for i in range(10)] + \
           [(1, 7), (4, 10),                        # the court's gaps
            (11, 20), (8, 15),                      # the middle's
            (16, 22), (21, 23), (13, 24)]           # the rooms
    homes = [list(range(6, 12)), list(range(12, 22)), list(range(0, 6))]
    graph = dict(nodes=nodes, legs=legs, homes=homes)
    resp = [(1024, 384, 256), (384, 1152, 0, 1), (1664, 1152, 512, 2),
            (1024, 1920, 768), (576, 1152, 768, 1), (1472, 1152, 256, 2),
            (768, 832, 256)]            # (the west half red, the east blue)
    pks = [((PK_ROCKET, PK_GRENADE), 1024, 1152),        # the stack's top
           ((PK_SHOTGUN, PK_CHAINGUN), 1920, 1152),      # the east room's
           ((PK_RAIL, PK_SHOTGUN), 128, 1152),           # the west room's
           ((PK_HEALTH, PK_SHIELD), 1024, 128)]          # the south room's
    start = (1024, 384, 256)
    bench = [("A", 768, 832, 128, 0, 0),         # the court, at the stack
             ("B", 1664, 1152, 0, 0, 0),         # into the east room
             ("C", 576, 576, 256, 0, 0),         # the inner ring's corner
             ("D", 288, 1600, 768, 0, MP)]       # on the outer wall, west
    return bx_, graph, resp, pks, start, bench


HEDGE_BOXES, HEDGE_GRAPH, HEDGE_RESPAWNS, HEDGE_PICKUPS, HEDGE_START, \
    HEDGE_BENCH = _hedge()


# --- CROSSING (docs/plans/completed/WIREARENA-THREE-ARENAS-PLAN.md 1, the owner's
# drawing of 2026-11-15): FLAT - every box 3 m, the renderer's and the
# collision's cheapest paths. A plus-shaped corridor 2 m wide through the
# middle, each arm 512 long, its sides 1 m walls - the sight grid's cell,
# which a tall box keeps to (four L-shaped corners,
# two boxes each), open at the arms' ends; a pillar in each quarter.
def _crossing():
    W, lo, hi, a0, a1 = 64, 512, 1536, 960, 1088   # (a wall, the arms' span,
    bx_ = [                                        # the corridor's sides)
        Box(a1, a1, a1 + W, hi, name="NE corner, N arm's side"),
        Box(a1 + W, a1, hi, a1 + W, name="NE corner, E arm's side"),
        Box(a0 - W, a1, a0, hi, name="NW corner, N arm's side"),
        Box(lo, a1, a0 - W, a1 + W, name="NW corner, W arm's side"),
        Box(a1, lo, a1 + W, a0, name="SE corner, S arm's side"),
        Box(a1 + W, a0 - W, hi, a0, name="SE corner, E arm's side"),
        Box(a0 - W, lo, a0, a0, name="SW corner, S arm's side"),
        Box(lo, a0 - W, a0 - W, a0, name="SW corner, W arm's side"),
        Box(320, 384, 448, 512, name="pillar SW"),
        Box(1600, 384, 1728, 512, name="pillar SE"),
        Box(320, 1536, 448, 1664, name="pillar NW"),
        Box(1600, 1536, 1728, 1664, name="pillar NE")]
    ring = [(192, 192), (1024, 192), (1856, 192), (1856, 1024),
            (1856, 1856), (1024, 1856), (192, 1856), (192, 1024)]   # 0-7
    arms = [(1024, 320), (1728, 1024), (1024, 1728), (320, 1024),
            (1024, 1024), (1344, 1344), (640, 640)]  # 8-11 the arms' ends,
    nodes = ring + arms                              # 12 the centre, 13-14
    legs = [(i, (i + 1) % 8) for i in range(8)] + \
           [(1, 8), (3, 9), (5, 10), (7, 11),       # (the ring to the arms,
            (8, 12), (9, 12), (10, 12), (11, 12),   # the arms in, and two
            (10, 13), (13, 9), (8, 14), (14, 11), (11, 8)]   # quarters')
    homes = [list(range(8)), [12, 10, 13, 9], [8, 14, 11]]
    graph = dict(nodes=nodes, legs=legs, homes=homes)
    resp = [(192, 1536, 0, 1), (192, 512, 0, 1),             # red: the west
            (1856, 1536, 512, 2), (1856, 512, 512, 2),       # blue: the east
            (1024, 1856, 768), (1024, 192, 256)]
    choice = (PK_CHAINGUN, PK_SHOTGUN, PK_RAIL)
    pks = [((PK_GRENADE, PK_ROCKET), 1024, 1408),   # in the crossing's arms
           ((PK_GRENADE, PK_ROCKET), 1024, 640),
           ((PK_HEALTH, PK_SHIELD), 1024, 960),     # its centre
           (choice, 192, 192), (choice, 1856, 192),  # the room's corners
           (choice, 1856, 1856), (choice, 192, 1856)]
    start = (1024, 192, 256)
    bench = [("A", 1024, 256, 256, 0, 0),           # up the south arm
             ("B", 192, 192, 128, 0, 0),            # a corner, across
             ("C", 1024, 1024, 0, 0, 0),            # the centre, east
             ("D", 1856, 1024, 512, 0, 0)]          # the blue flag, west
    return bx_, graph, resp, pks, start, bench


CROSS_BOXES, CROSS_GRAPH, CROSS_RESPAWNS, CROSS_PICKUPS, CROSS_START, \
    CROSS_BENCH = _crossing()


# --- THE ZIGGURAT (THREE-ARENAS-PLAN 3, the owner's drawing of 2026-11-15):
# very high walls (8 m), the room's corners cut by tall boxes stepped three
# to a corner (four cost 11% a frame more, two were coarse) - the drawing's octagon in the boxes the format has; a tower
# 4 m square and 4.5 m tall (15 treads of a step's 24 the most a flight
# climbs; 12 here) with a ramp up each side; a pillar between it and each
# corner, the corner's spawn and launcher behind it, out of the tower's
# sight. No flags.
ZH = 512                        # the walls, the corners' boxes, the pillars
ZT = 288                        # the tower


def _ziggurat():
    bx_ = []
    for (fx, fy) in ((0, 0), (1, 0), (0, 1), (1, 1)):   # the corners' steps
        for k in range(3):
            x0, x1 = 0, 384 - 128 * k
            y0, y1 = 128 * k, 128 * (k + 1)
            if fx:
                x0, x1 = 2048 - x1, 2048 - x0
            if fy:
                y0, y1 = 2048 - y1, 2048 - y0
            bx_.append(Box(x0, y0, x1, y1, ZH, col=COL_ROOM,
                           name="corner %d%d step %d" % (fx, fy, k)))
    bx_ += [
        Box(896, 896, 1152, 1152, ZT, col=COL_PLAT, name="the tower"),
        Box(960, 1152, 1088, 1536, ZT, FLIGHT, col=COL_PLAT, toward="S",
            treads=12, name="north ramp"),
        Box(960, 512, 1088, 896, ZT, FLIGHT, col=COL_PLAT, toward="N",
            treads=12, name="south ramp"),
        Box(1152, 960, 1536, 1088, ZT, FLIGHT, col=COL_PLAT, toward="W",
            treads=12, name="east ramp"),
        Box(512, 960, 896, 1088, ZT, FLIGHT, col=COL_PLAT, toward="E",
            treads=12, name="west ramp"),
        Box(448, 448, 576, 576, ZH, name="pillar SW"),
        Box(1472, 448, 1600, 576, ZH, name="pillar SE"),
        Box(448, 1472, 576, 1600, ZH, name="pillar NW"),
        Box(1472, 1472, 1600, 1600, ZH, name="pillar NE")]
    ring = [(320, 320), (1024, 256), (1728, 320), (1792, 1024),
            (1728, 1728), (1024, 1792), (320, 1728), (256, 1024)]  # 0-7
    feet = [(1024, 448), (1600, 1024), (1024, 1600), (448, 1024)]  # 8-11
    nodes = ring + feet + [(1024, 1024), (640, 1600)]   # 12 the top, 13
    legs = [(i, (i + 1) % 8) for i in range(8)] + \
           [(1, 8), (3, 9), (5, 10), (7, 11),       # (the ring to the feet,
            (8, 12), (9, 12), (10, 12), (11, 12),   # the ramps, and two
            (9, 8), (11, 13), (13, 10), (10, 11)]   # loops of the feet)
    homes = [list(range(8)), [12, 9, 8], [11, 13, 10]]
    graph = dict(nodes=nodes, legs=legs, homes=homes)
    resp = [(320, 320, 128, 1), (320, 1728, 896, 1),          # red: the west
            (1728, 320, 384, 2), (1728, 1728, 640, 2)]       # blue: the east
    corner = (PK_CHAINGUN, PK_ROCKET)
    pks = [((PK_HEALTH, PK_SHIELD), 1024, 944),     # the tower's top: the
           (PK_RAIL, 1024, 1104),                   # owner's three, two
           (corner, 384, 384), (corner, 1664, 384),  # spawns; the corners
           (corner, 384, 1664), (corner, 1664, 1664)]
    start = (1024, 256, 256)
    bench = [("A", 1024, 256, 256, 0, 0),           # the tower, from south
             ("B", 320, 320, 128, 0, 0),            # a corner, its pillar
             ("C", 1024, 1024, 0, -40, ZT),         # on top, east
             ("D", 1792, 1024, 512, 0, 0)]          # the east ramp's foot
    return bx_, graph, resp, pks, start, bench


ZIGG_BOXES, ZIGG_GRAPH, ZIGG_RESPAWNS, ZIGG_PICKUPS, ZIGG_START, \
    ZIGG_BENCH = _ziggurat()


# --- THE PIT (THREE-ARENAS-PLAN 2, the owner's drawing of 2026-11-15): three
# levels - the pit's floor (0), the second layer round it (PS, deeper than
# an eye: from the pit its sides are walls) and the top ring round that (PR,
# a hop above the second layer) - and no room walls: the arena stands in the
# dark. Tall dividers north and south across the ring and the second layer
# leave the pit the one way from the west half to the east. A lift on the
# pit's west and east sides out; no health - the four spawns on the second
# layer are any weapon.
PS = 192                        # the second layer
PR = 240                        # the top ring (48 up: the jump's 69 clears)
PW = 448                        # the dividers


def _pit():
    S, R, P, W = COL_ROOM, COL_ITEM, COL_PLAT, COL_HEDGE
    bx_ = [
        Box(0, 0, 128, 2048, PR, col=S, name="ring W"),
        Box(1920, 0, 2048, 2048, PR, col=S, name="ring E"),
        Box(128, 1920, 576, 2048, PR, col=S, name="ring N-W"),
        Box(1472, 1920, 1920, 2048, PR, col=S, name="ring N-E"),
        Box(128, 0, 576, 128, PR, col=S, name="ring S-W"),
        Box(1472, 0, 1920, 128, PR, col=S, name="ring S-E"),
        Box(576, 1920, 1472, 2048, PW, col=W, name="divider N, the ring's"),
        Box(576, 0, 1472, 128, PW, col=W, name="divider S, the ring's"),
        Box(128, 128, 384, 1920, PS, col=P, name="layer W"),
        Box(1664, 128, 1920, 1920, PS, col=P, name="layer E"),
        Box(384, 1536, 896, 1920, PS, col=P, name="layer N-W"),
        Box(1152, 1536, 1664, 1920, PS, col=P, name="layer N-E"),
        Box(384, 128, 896, 512, PS, col=P, name="layer S-W"),
        Box(1152, 128, 1664, 512, PS, col=P, name="layer S-E"),
        Box(896, 1536, 1152, 1920, PW, col=W, name="divider N, the layer's"),
        Box(896, 128, 1152, 512, PW, col=W, name="divider S, the layer's"),
        Box(384, 896, 448, 1152, 16, LIFT, col=R, top=PS, name="lift W"),
        Box(1600, 896, 1664, 1152, 16, LIFT, col=R, top=PS, name="lift E")]
    pit = [(512, 640), (1536, 640), (1536, 1024), (1536, 1408),
           (512, 1408), (512, 1024), (1024, 1024)]      # 0-6 (6 the centre)
    west = [(192, 512), (320, 512), (320, 1024), (320, 1536), (192, 1536),
            (192, 1024)]                                # 7-12
    east = [(1856, 512), (1728, 512), (1728, 1024), (1728, 1536),
            (1856, 1536), (1856, 1024)]                 # 13-18
    more = [(640, 320), (640, 1728), (1408, 320), (1408, 1728),   # 19-22
            (64, 1024), (1984, 1024),                   # 23-24 the flags
            (416, 1024), (1632, 1024)]                  # 25-26 the lifts
    nodes = pit + west + east + more
    legs = [(i, (i + 1) % 6) for i in range(6)] + \
           [(7 + i, 7 + (i + 1) % 6) for i in range(6)] + \
           [(13 + i, 13 + (i + 1) % 6) for i in range(6)] + \
           [(6, 5), (6, 2),                              # (the centre)
            (8, 19), (10, 20), (14, 21), (16, 22),       # (the layer's ends)
            (19, 0, DROP), (20, 4, DROP),                # (down into the pit)
            (21, 1, DROP), (22, 3, DROP),
            (5, 25, LIFT_ON), (25, 9, LIFT_UP),          # (out by the lifts)
            (2, 26, LIFT_ON), (26, 15, LIFT_UP),
            (12, 23, HOP), (23, 12, DROP),               # (onto the ring, to
            (18, 24, HOP), (24, 18, DROP)]               # the flags)
    homes = [list(range(6)), list(range(7, 13)), list(range(13, 19))]
    graph = dict(nodes=nodes, legs=legs, homes=homes)
    resp = [(256, 768, 0, 1), (256, 1280, 0, 1), (64, 512, 0, 1),
            (1792, 768, 512, 2), (1792, 1280, 512, 2), (1984, 1536, 512, 2)]
    any_ = (PK_SHOTGUN, PK_CHAINGUN, PK_GRENADE, PK_ROCKET, PK_RAIL)
    pks = [(any_, 768, 320), (any_, 1280, 320),
           (any_, 768, 1728), (any_, 1280, 1728)]
    start = (256, 1024, 0)
    bench = [("A", 256, 1024, 0, -40, PS),          # the west layer, east
             ("B", 64, 1024, 0, -40, PR),           # the red flag, the ring
             ("C", 1024, 1024, 0, 0, 0),            # the pit's centre, east
             ("D", 640, 1728, 0, -40, PS)]          # the north layer, east
    return bx_, graph, resp, pks, start, bench


PIT_BOXES, PIT_GRAPH, PIT_RESPAWNS, PIT_PICKUPS, PIT_START, \
    PIT_BENCH = _pit()


def arenas():
    return [("a0", "Arena", BOXES, PLAYER_START),
            ("a1", "Open", OPEN_BOXES, PLAYER_START),
            ("a2", "Heights", HEIGHTS_BOXES, PLAYER_START),
            ("a3", "Hedge", HEDGE_BOXES, HEDGE_START),
            ("a4", "Twin", TWIN_BOXES, TWIN_START),
            ("a5", "Crossing", CROSS_BOXES, CROSS_START),
            ("a6", "Ziggurat", ZIGG_BOXES, ZIGG_START),
            ("a7", "Pit", PIT_BOXES, PIT_START)]


# THE SETUP'S LIST (THREE-ARENAS-PLAN 4): the arenas in the order the Arena
# drop-down shows them - an arena keeps its number (the benches, the poses,
# the rows and the wire all name them); WA_ORD<item> is the item's arena
ARENA_ORDER = ["a1", "a0", "a5", "a2", "a3", "a4", "a7", "a6"]


# an arena's own patrols (the rest walk PATHS): every arena's are WA_NFIG
# loops - an arena with no route graph of its own walks them as one
ARENA_PATHS = {"a2": HEIGHTS_PATHS}
ARENA_GRAPHS = {"a2": HEIGHTS_GRAPH, "a3": HEDGE_GRAPH, "a4": TWIN_GRAPH,
                "a5": CROSS_GRAPH, "a6": ZIGG_GRAPH, "a7": PIT_GRAPH}
ARENA_ROOMCOL = {}                      # (a room's walls not the room's blue)
# CTF's BASES (CTF-PLAN 3), red's then blue's, on a route node: the twin's
# two flag places, behind its lifts; the hedge's west and east rooms
ARENA_FLAGS = {"a3": [(224, 1152), (1824, 1152)], "a4": TWIN_FLAGS,
               "a5": [(192, 1024), (1856, 1024)],
               "a7": [(64, 1024), (1984, 1024)]}
# FLOOR LINES (ARENAS-PLAN 4.5): an arena's closed lines on the floor, the
# feet's edge - and its room's height, 0 for no room walls at all
ARENA_FLINES = {"a4": TWIN_WALK}
ARENA_ROOMH = {"a3": MH, "a4": 0, "a6": ZH, "a7": 0}      # (the hedge's edge all of a height,
                                        # the owner's: its fills' and its
                                        # room's walls - HEDGES-PLAN 9)
WA_MFL = 24                             # (wafb.inc's)
WA_MFV = 24                             # (wafb.inc's: their distinct ends)


def flines(tag):
    """an arena's floor lines as segments (x0, y0, x1, y1)"""
    out = []
    for poly in ARENA_FLINES.get(tag, []):
        out += [poly[i] + poly[(i + 1) % len(poly)] for i in range(len(poly))]
    return out


def fl_cross(p, q, segs):
    """does the step p -> q touch or cross any of segs (wa_flcross's rule,
    with touching counted)"""
    def cr(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    for (x0, y0, x1, y1) in segs:
        a, b = (x0, y0), (x1, y1)
        d1, d2 = cr(a, b, p), cr(a, b, q)
        d3, d4 = cr(p, q, a), cr(p, q, b)
        if (d1 < 0) != (d2 < 0) and (d3 == 0 or d4 == 0 or
                                     (d3 < 0) != (d4 < 0)):
            return True
    return False


def in_walk(tag, x, y, r=0):
    """inside the arena's floor lines (any point, with none), and r clear of
    every one of them"""
    segs = flines(tag)
    if not segs:
        return True
    n = 0
    for (x0, y0, x1, y1) in segs:
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            n += 1
        dx, dy = x1 - x0, y1 - y0
        t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
        if math.hypot(x - x0 - t * dx, y - y0 - t * dy) < r:
            return False
    return n & 1 == 1
# arenas let past the visible budget (wamodel.BUDGET_*, HEIGHTS-PLAN 4.2) -
# the owner's stress tests (ARENAS-PLAN 4.4: "to see how well the system holds
# up"), each with its own ceiling and its frame rate measured where it is
# worst. (boxes, outline edges)
OVER_BUDGET = {}
ARENA_RESPAWNS.update({"a3": HEDGE_RESPAWNS, "a4": TWIN_RESPAWNS,
                       "a5": CROSS_RESPAWNS, "a6": ZIGG_RESPAWNS,
                       "a7": PIT_RESPAWNS})
# THE PLAY BENCH'S VIEWS (P, MULTIPLAYER-FPS-PLAN 16.7; ARENAS-PLAN 4.2), an
# arena's own, each P press the next: mark, x, y, angle (1024ths), base
# pitch, the floor under the eye. The heights arena's five are HEIGHTS-PLAN
# 9's A to E; the hedge's and the twin's their worst and their landmarks
_PB = ("P", 224, 800, 32, 0, 0)         # (across the first two rooms)
ARENA_BENCH = {
    "a0": [_PB], "a1": [_PB],
    "a2": [("A", 800, 400, 160, 0, 0), ("B", 1600, 1024, 512, 0, 0),
           ("C", 1248, 1024, 512, 0, 80), ("D", 1952, 1024, 512, -40, 160),
           ("E", 600, 1568, 512, 0, 0)],
    "a3": HEDGE_BENCH,                  # (the hedge's: its _hedge)
    "a5": CROSS_BENCH, "a6": ZIGG_BENCH, "a7": PIT_BENCH,
    "a4": [("A", 640, 1024, 0, 0, 0),           # at the path, east along it
           ("B", 512, 1024, 0, -40, 240),       # on the west tower
           ("C", 1024, 832, 0, 0, 0),           # the path's bottom
           ("D", 192, 1024, 0, 0, 0)],          # behind the lift: the flag
}


def height_at(b, x, y, r):
    """Box b's height under the square (x +- r, y +- r), which overlaps it -
    for a RAMP (a FLIGHT) the height at the square's highest point on it,
    the slope linear from 0 at its low face to b.h at the face it rises
    TOWARD: the game's wa_hat, exactly (integer, rounded down)"""
    b = bx(b)
    if b.kind != FLIGHT:
        return b.h
    x0, y0, x1, y1 = b
    if b.toward == "N":
        num, ln = min(y + r, y1) - y0, y1 - y0
    elif b.toward == "S":
        num, ln = y1 - max(y - r, y0), y1 - y0
    elif b.toward == "E":
        num, ln = min(x + r, x1) - x0, x1 - x0
    else:
        num, ln = x1 - max(x - r, x0), x1 - x0
    return int(b.h * num) // ln


def floor_at(x, y, boxes, r):
    """The floor under the square (x +- r, y +- r): the tallest height of
    any box it overlaps, or 0 (wa_floor)"""
    f = 0
    for b in boxes:
        if b[0] - r < x < b[2] + r and b[1] - r < y < b[3] + r:
            f = max(f, height_at(b, x, y, r))
    return f


def room_h(boxes):
    return WALL_H


def hmax(boxes):
    """The arena's tallest height, the room's and the lifts' raised included:
    a face at it, seen from below it, shuts its columns (HEIGHTS-PLAN 1.2)"""
    return max([room_h(boxes)] + [max(b.h, b.top or 0) for b in map(bx, boxes)])

# --- the potentially-visible sets (docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 9.8) --
# Per cell of a square grid over the room, the boxes that can draw anything
# from SOMEWHERE in the cell: the renderer's walk (wa_order) skips the rest
# before their cone test, transforms or setup. Only TALL boxes occlude - a box
# at the arena's tallest, which every eye the game allows is below
# (pvs_eyemax), so occlusion is a question in 2D: a box is visible from a
# point when some ray from it reaches the box before any tall box in front of
# it. A box draws only in columns whose centre ray meets it, and a column a
# nearer tall face covers is shut, so a box no ray reaches draws nothing.
# Conservative against the renderer's own rounding: each box's angular
# interval widened, each occluder's narrowed, by PVS_EPS radians (a column's
# projection truncates to under 1/16 of a column: 0.0025 rad at 90 degrees and
# 40 columns, the widest) plus PVS_EPSD / distance (a camera-space coordinate
# truncates by under a unit in x and in z, under 2 / z rad on the screen; and
# a point tested is within 2 units of any on its outline). From a cell, the
# points tested are its outline and any tall box's outline (a unit out)
# inside it at PVS_STEP units, and a PVS_GRIDP grid inside: a ray from an
# inner point reaches the box from where, run backwards, it leaves the cell
# or meets an occluder's face, as well.
PVS_EPS = 0.003
PVS_EPSD = 5.0
PVS_STEP = 4
PVS_GRIDP = 16
PVS_MIN = 0.02                  # an arena carries one when it culls at
                                # least this share of its box-cells: the
                                # masks cost its part ~1 KB (~110 bytes
                                # packed) and a frame nothing but a bit a
                                # leaf when the order is made, and the
                                # full arena's 2.6% measured -0.4% render
                                # at 40 columns (pass 7, the data's) - the
                                # open arena's 0% carries none
PVS_SHIFT = 7                   # the grid: 128 units (2 m) a cell


def adjacent(a, b):
    """Boxes a and b share a stretch of face"""
    return ((a[2] == b[0] or b[2] == a[0]) and a[1] < b[3] and b[1] < a[3]) \
        or ((a[3] == b[1] or b[3] == a[1]) and a[0] < b[2] and b[0] < a[2])


def climbed(n, boxes):
    """A tall box n a flight or a lift beside it climbs onto (the twin's
    towers): stood on, so a floor, not a wall"""
    return any(b.kind in (FLIGHT, LIFT) and adjacent(b, n) and
               n.h - max(b.h, b.top or 0) <= STEPUP for b in boxes)


def pvs_eyemax(boxes):
    """The highest eye the game allows: the highest floor anything stands
    on (a low box's top, a lift raised, a TALL box a flight or a lift
    climbs to - the twin's towers, ARENAS-PLAN 3), the eye above it, and a
    jump"""
    top = hmax(boxes)
    floor = max([0] + [max(b.h, b.top or 0) for b in boxes if b.h < top
                       or b.kind == LIFT] +
                [n.h for b in boxes if b.kind in (FLIGHT, LIFT)
                 for n in boxes if n is not b and n.h >= top and
                 adjacent(b, n) and n.h - max(b.h, b.top or 0) <= STEPUP])
    src = open(__file__.replace("tools/arenagen.py", "apps/wirearena/waconst.inc")
               if __file__.endswith("tools/arenagen.py") else
               "apps/wirearena/waconst.inc").read()
    eye = int(re.search(r"WA_EYE\s+equ\s+(\d+)", src).group(1))
    return floor + eye + jump_apex()


def pvs_interval(p, r, ref):
    """Box r's angular interval seen from p, relative to the direction ref,
    and its distance - None when p is in it"""
    a, b, c, d = r
    px, py = p
    if a <= px <= c and b <= py <= d:
        return None
    cd = math.atan2((b + d) / 2 - py, (a + c) / 2 - px)
    ts = [(math.atan2(y - py, x - px) - cd + math.pi) % (2 * math.pi) - math.pi
          for (x, y) in ((a, b), (c, b), (c, d), (a, d))]
    s = (cd - ref + math.pi) % (2 * math.pi) - math.pi
    return (min(ts) + s, max(ts) + s,
            math.hypot(max(a - px, 0, px - c), max(b - py, 0, py - d)))


def pvs_front(p, o, b):
    """Box o can stand in front of box b on a ray from p: p is on o's side
    of every axis line that separates them (on b's side of any, b is met
    first on any ray meeting both)"""
    px, py = p
    if b[2] <= o[0] and not px > o[0]:
        return False
    if o[2] <= b[0] and not px < o[2]:
        return False
    if b[3] <= o[1] and not py > o[1]:
        return False
    if o[3] <= b[1] and not py < o[3]:
        return False
    return True


def pvs_seen(p, boxes, tall, have):
    """-> the boxes (bits) not in have that p sees some part of"""
    got = 0
    for i, B in enumerate(boxes):
        if have >> i & 1:
            continue
        ref = math.atan2((B[1] + B[3]) / 2 - p[1], (B[0] + B[2]) / 2 - p[0])
        bi = pvs_interval(p, B, ref)
        if bi is None:
            got |= 1 << i
            continue
        e = PVS_EPS + PVS_EPSD / max(bi[2], 1.0)
        lo, hi = bi[0] - e, bi[1] + e
        cov = []
        for j in tall:
            if j != i and pvs_front(p, boxes[j], B):
                oi = pvs_interval(p, boxes[j], ref)
                if oi is None:
                    continue            # (never: no point is in a tall box)
                f = PVS_EPS + PVS_EPSD / max(oi[2], 1.0)
                a, b = oi[0] + f, oi[1] - f
                for k in (-2 * math.pi, 0, 2 * math.pi):
                    if a < b and a + k < hi and b + k > lo:
                        cov.append((a + k, b + k))
        at = lo
        for a, b in sorted(cov):
            if a > at:
                break
            at = max(at, b)
        if at < hi:
            got |= 1 << i
    return got


_PVS = {}


def pvs_masks(tag, boxes):
    """-> the grid's masks, [ix][iy] (bit i: box i may draw from the cell),
    or None when the arena takes none (made once a run: tools/wamodel.py
    walks with them too)"""
    if tag not in _PVS:
        _PVS[tag] = pvs_build(tag, boxes)
    return _PVS[tag]


def pvs_build(tag, boxes):
    boxes = [bx(b) for b in boxes]
    top = hmax(boxes)
    if pvs_eyemax(boxes) >= top:
        return None                     # (an eye over the tallest: no occluder)
    tall = [i for i, b in enumerate(boxes) if b.h >= top]
    occ = [boxes[i] for i in tall]
    m = pvs_reach()                     # (every place the game puts an eye
    for (x, y) in ([tuple(arenas()[[t for t, _, _, _ in arenas()].index(tag)]
                          [3][:2]), (1024, 700)] +      # without walking -
                   [tuple(r[:2]) for r in ARENA_RESPAWNS.get(  # the start,
                       tag, FLAT_RESPAWNS)] +           # the B bench, the
                   [tuple(v[1:3]) for v in ARENA_BENCH.get(tag, [])]):  # re-
        if not (ROOM[0] + m <= x <= ROOM[2] - m and     # spawns, the P
                ROOM[1] + m <= y <= ROOM[3] - m) or any(  # bench's views -
                a - m < x < c + m and b - m < y < d + m     # is in the cells'
                for (a, b, c, d) in occ):                   # reach, pvs_points)
            raise SystemExit("arenagen: arena %s: an eye placed at (%d, %d), "
                             "within %d of a tall box or the wall - outside "
                             "what the PVS is made for" % (tag, x, y, m))
    g = 1 << PVS_SHIFT
    if (ROOM[2] - ROOM[0]) % g or (ROOM[3] - ROOM[1]) % g or ROOM[0] or ROOM[1]:
        raise SystemExit("arenagen: the PVS grid wants the room at 0, 0 in "
                         "whole cells")
    full = (1 << len(boxes)) - 1
    out, cut = [], 0
    for x0 in range(ROOM[0], ROOM[2], g):
        col = []
        for y0 in range(ROOM[1], ROOM[3], g):
            vis = 0
            for p in pvs_points(x0, y0, x0 + g, y0 + g, occ):
                if vis == full:
                    break
                vis |= pvs_seen(p, boxes, tall, vis)
            col.append(vis)
            cut += bin(full & ~vis).count("1")
        out.append(col)
    low = 0                             # (what the LOW boxes hide as well -
    if os.environ.get("WA_PVSLOW", "1") != "0":   # WA_PVSLOW=0: the A/B, the
        low = pvs_low(boxes, out, occ)            # tall boxes' test alone)
    cut += low
    for i in range(len(boxes) - 1, -1, -1):   # A STACK is drawn from its
        b = boxes[i]                    # bottom's leaf, each box from the one
        if b.on is not None:            # it stands on (wl_child): a box that
            r = i - 1                   # can be seen keeps every box under
            for col in out:             # it (HEDGES-PLAN 8 - a tall tier
                for k in range(len(col)):   # over a hedge its bottom is not),
                    if col[k] >> i & 1:     # top down, so the bottom takes
                        col[k] |= 1 << r    # them all
    share = cut / float(len(boxes) * len(out) * len(out[0]))
    sys.stderr.write("arenagen: arena %s: the PVS culls %.1f%% of box-cells "
                     "(%.1f%% behind low boxes)%s\n"
                     % (tag, 100 * share, 100.0 * low /
                        (len(boxes) * len(out) * len(out[0])),
                        "" if share >= PVS_MIN else
                        " - under %d%%, none carried" % (100 * PVS_MIN)))
    return out if share >= PVS_MIN else None


# ...AND WHAT THE LOW BOXES HIDE (docs/plans/MULTIPLAYER-FPS-PLAN.md 16.10: the
# owner, "data in the map that tells us ahead of time what you can't see").
# The tall boxes' test above is flat: a box below the arena's tallest hides
# nothing in it, because an eye can see over it. But how MUCH it can see over
# depends on how high the eye is, and that is known per cell: the boxes are a
# HEIGHTFIELD, and over a heightfield a higher eye sees everything a lower one
# does - the sight line to any point only rises. So from a cell, at the
# HIGHEST eye a player can have there (pvs_eyes), a box that no sight line
# reaches is drawn from nowhere in the cell: the ramp behind the platform,
# from the floor, cost ~10,700 clk a frame of view A to draw nothing.
# Along one ray from the eye a box's top spans [tin, tout]; its best chance
# is the end of that with the steeper sight line (a level top's far end from
# above, its near end from below; a ramp's either), and it is hidden when a
# nearer box's top stands at least as steep - its near end from below, its
# far end from above. Rays are cast from pvs_points' points across the box's
# whole angle, no more than PVS_LSTEP units apart at the box.
# This is the geometry's answer with no margin, where the tall test is
# conservative against the renderer's rounding: a box the low test drops is
# HIDDEN, or touches what hides it (a ramp's top edge along the platform's),
# so the most the picture can lose is a cell the rounding had let through.
# A lift hides at its REST height and is looked for at its raised one; the
# room's walls hide nothing. Only boxes BELOW the tallest are looked for
# here - a tall box is the flat test's alone, margins and all - and a tall
# box in the way hides with those same margins.
PVS_LSTEP = 6.0                 # rays across a box: this far apart at it
PVS_LMAX = 64                   # ...and no more than this many
PVS_LPTS = 2                    # every this-many'th of pvs_points' points


def pvs_top(b, x, y, raised):
    """Box b's top over (x, y): a ramp's slope there (arenagen's height_at
    without its square), a lift's raised height or its rest one"""
    if b.kind == FLIGHT:
        x0, y0, x1, y1 = b
        f = {"N": (y - y0) / float(y1 - y0), "S": (y1 - y) / float(y1 - y0),
             "E": (x - x0) / float(x1 - x0), "W": (x1 - x) / float(x1 - x0)}[b.toward]
        return b.h * min(1.0, max(0.0, f))
    if b.kind == LIFT and raised:
        return max(b.h, b.top or 0)
    return b.h


def pvs_eyes(boxes, n):
    """-> [ix][iy], the highest eye a player can have in each cell: the eye
    over the highest floor a square of the player's radius can stand on in
    the cell, a jump more - or what is left of a jump off a higher floor k
    cells away, by the time its fall has covered the k - 1 cells between
    (the game's own gravity and jump, off waconst.inc; a step along one axis
    is at most the move's speed x sqrt 2)"""
    src = open(__file__.replace("tools/arenagen.py", "apps/wirearena/waconst.inc")
               if __file__.endswith("tools/arenagen.py") else
               "apps/wirearena/waconst.inc").read()
    k = lambda name: int(re.search(name + r"\s+equ\s+(\d+)", src).group(1))
    eye, rad = k("WA_EYE"), k("WA_RAD")
    v, g = k("WA_JUMPV") / 256.0, k("WA_GRAV") / 256.0
    speed = k("WA_MSPD") / 8.0 * math.sqrt(2)   # (Q13 x WA_MSPD: units a tick)
    cell = 1 << PVS_SHIFT
    top = hmax(boxes)
    floor = [[0] * n for _ in range(n)]
    for b in boxes:
        if b.h >= top and b.kind != LIFT:
            continue
        h = max(b.h, b.top or 0)
        for ix in range(max(0, (b[0] - rad) // cell), min(n - 1, (b[2] + rad) // cell) + 1):
            for iy in range(max(0, (b[1] - rad) // cell), min(n - 1, (b[3] + rad) // cell) + 1):
                floor[ix][iy] = max(floor[ix][iy], h)
    apex = jump_apex()

    def rise(kc):
        t = max(0, kc - 1) * cell / speed       # ticks to cover the cells between
        return apex if t <= v / g else v * t - g * t * t / 2
    out = [[0] * n for _ in range(n)]
    for ix in range(n):
        for iy in range(n):
            out[ix][iy] = max(floor[jx][jy] + eye + rise(max(abs(ix - jx), abs(iy - jy)))
                              for jx in range(n) for jy in range(n))
    return out


def pvs_span(px, py, ux, uy, b):
    """The ray's (t in, t out) through box b's footprint, or None"""
    t0, t1 = -1e18, 1e18
    for o, u, lo, hi in ((px, ux, b[0], b[2]), (py, uy, b[1], b[3])):
        if abs(u) < 1e-12:
            if not lo <= o <= hi:
                return None
            continue
        ta, tb = (lo - o) / u, (hi - o) / u
        if ta > tb:
            ta, tb = tb, ta
        if ta > t0:
            t0 = ta
        if tb < t1:
            t1 = tb
    return (t0, t1) if t0 <= t1 and t1 > 0 else None


def pvs_lowseen(p, e, i, boxes, top):
    """A sight line from p, the eye e high, reaches box i over the others. A
    TALL box in the way hides only inside its angle NARROWED as the flat test
    narrows it (PVS_EPS, PVS_EPSD: the renderer's columns round), so nothing
    is dropped here for a sliver the flat test would have kept"""
    px, py = p
    B = boxes[i]
    if B[0] - 1 <= px <= B[2] + 1 and B[1] - 1 <= py <= B[3] + 1:
        return True                     # (on it, or against it)
    cd = math.atan2((B[1] + B[3]) / 2.0 - py, (B[0] + B[2]) / 2.0 - px)
    ts = [(math.atan2(y - py, x - px) - cd + math.pi) % (2 * math.pi) - math.pi
          for (x, y) in ((B[0], B[1]), (B[2], B[1]), (B[2], B[3]), (B[0], B[3]))]
    lo, hi = min(ts) + 1e-7, max(ts) - 1e-7
    far = max(math.hypot(x - px, y - py)
              for (x, y) in ((B[0], B[1]), (B[2], B[1]), (B[2], B[3]), (B[0], B[3])))
    n = int(min(PVS_LMAX, max(2, math.ceil((hi - lo) * far / PVS_LSTEP))))
    occ = []
    for j, o in enumerate(boxes):
        if j == i or not pvs_front(p, o, B):
            continue
        if o.h < top:
            occ.append((o, None))
            continue
        oi = pvs_interval(p, o, cd)
        if oi is not None:
            f = PVS_EPS + PVS_EPSD / max(oi[2], 1.0)
            if oi[0] + f < oi[1] - f:
                occ.append((o, (oi[0] + f, oi[1] - f)))
    for kk in range(n + 1):
        ar = lo + (hi - lo) * kk / n
        a = cd + ar
        ux, uy = math.cos(a), math.sin(a)
        sp = pvs_span(px, py, ux, uy, B)
        if sp is None:
            continue
        best = -1e18                    # the box's steepest sight line
        for t in sp:
            if t > 1e-6:
                best = max(best, (pvs_top(B, px + ux * t, py + uy * t, True) - e) / t)
        hid = False
        for o, within in occ:
            if within is not None and not any(
                    within[0] < ar + w < within[1]
                    for w in (-2 * math.pi, 0, 2 * math.pi)):
                continue
            so = pvs_span(px, py, ux, uy, o)
            if so is None or so[0] >= sp[0] - 1e-6 and so[0] > 1e-6:
                continue                # (missed, or behind the box)
            if so[1] > sp[0]:           # (only what is IN FRONT of it can
                so = (so[0], sp[0])     # hide it: a stack's box under it
            for t in so:                # runs on past it - HEDGES-PLAN 8)
                if t <= 1e-6:
                    continue            # (under the eye: its far end only)
                if (pvs_top(o, px + ux * t, py + uy * t, False) - e) / t >= best - 1e-9:
                    hid = True
                    break
            if hid:
                break
        if not hid:
            return True
    return False


def pvs_low(boxes, masks, occ):
    """Take out of masks every box the low test hides from the whole of its
    cell; -> how many box-cells it took"""
    g = 1 << PVS_SHIFT
    eyes = pvs_eyes(boxes, len(masks))
    top = hmax(boxes)
    cut = 0
    for ix, col in enumerate(masks):
        for iy, vis in enumerate(col):
            pts = pvs_points(ix * g, iy * g, ix * g + g, iy * g + g, occ)
            pts = pts[::PVS_LPTS] + pts[-1:]
            for i in range(len(boxes)):
                if boxes[i].h >= top:
                    continue            # (a tall box: the flat test's alone)
                if vis >> i & 1 and not any(pvs_lowseen(p, eyes[ix][iy], i, boxes, top)
                                            for p in pts):
                    vis &= ~(1 << i)
                    cut += 1
            col[iy] = vis
    return cut


def pvs_reach():
    """How near a tall box or the room's wall an eye can be, units: the
    player's square of WA_RAD collides with every box widened by it and
    keeps it off the wall (wa_collide, wa_lists), less 2 for the exact
    bounds of those compares"""
    src = open(__file__.replace("tools/arenagen.py", "apps/wirearena/waconst.inc")
               if __file__.endswith("tools/arenagen.py") else
               "apps/wirearena/waconst.inc").read()
    return int(re.search(r"WA_RAD\s+equ\s+(\d+)", src).group(1)) - 2


def pvs_points(x0, y0, x1, y1, occ):
    """The points a cell's visibility is tested from, sorted (pvs_build's
    comment): its outline and the occluders' inside it every PVS_STEP, and
    a PVS_GRIDP grid - none inside an occluder (no one stands in a wall).
    THE REACHABLE PART of the cell: an eye is never within pvs_reach() of a
    tall box or of the room's wall (the player's square collides first),
    so the cell is cut to the room less that, and each occluder widened by
    it - and what a point there sees is what the cell can (a ray from an
    eye, run backwards, leaves that region through its outline as it
    would leave the cell)"""
    m = pvs_reach()
    x0, y0 = max(x0, ROOM[0] + m), max(y0, ROOM[1] + m)
    x1, y1 = min(x1, ROOM[2] - m), min(y1, ROOM[3] - m)
    if x0 > x1 or y0 > y1:
        return []
    occ = [(a - m + 1, b - m + 1, c + m - 1, d + m - 1) for (a, b, c, d) in occ]
    pts = set()

    def edge(ax, ay, bx_, by):
        k = max(1, -(-max(abs(bx_ - ax), abs(by - ay)) // PVS_STEP))
        for t in range(k + 1):
            pts.add((ax + (bx_ - ax) * t / k, ay + (by - ay) * t / k))
    for (a, b, c, d) in [(x0, y0, x1, y1)] + [(a - 1, b - 1, c + 1, d + 1)
                                              for (a, b, c, d) in occ]:
        a, b, c, d = max(a, x0), max(b, y0), min(c, x1), min(d, y1)
        if a <= c and b <= d:
            edge(a, b, c, b), edge(c, b, c, d), edge(c, d, a, d), edge(a, d, a, b)
    for gx in range(x0, x1 + 1, PVS_GRIDP):
        for gy in range(y0, y1 + 1, PVS_GRIDP):
            pts.add((gx, gy))
    return [p for p in sorted(pts)
            if not any(a <= p[0] <= c and b <= p[1] <= d for (a, b, c, d) in occ)]


# --- pickups (docs/plans/completed/WIREARENA-WEAPONS-PLAN.md 2.4) ----------------------
# kind: the renderer's shape and colour (wa_pkdraw's table) and the game's
# effect (wa_pktake's); a weapon's kind is its weapon number less one
# (PK_* kinds: with the box kinds, at the top - the arenas name them)
PK_HALF = (24, 28, 20, 32, 40, 16, 12)  # half its width (wa_pkshape's)
PK_PVSR = 16                    # the square its PVS bit is for: at least
                                # every half width but the lines' - a line
                                # is drawn only where its middle's column is
                                # open, so its middle is what must be seen
WA_MPK = 8                      # the most an arena carries: a bit each
# Arena 2's (the owner's placement, 2026-11-13): the health pack in the back
# tunnel (the corridor along the west wall, behind the annex, opposite the
# back room's mouth); the shield pack on the jump platform in the back room;
# on the ramp platform a WEAPON SPAWN - the shotgun, the chaingun or the
# grenade launcher, picked at random each time it spawns - and on the
# tower's top (the platform the lift goes up to) the rocket launcher or the
# railgun (the owner, 2026-11-14). A kind, or a tuple of kinds to pick from
HEIGHTS_PICKUPS = [
    (PK_HEALTH, 64, 1024),
    (PK_SHIELD, 480, 1024),
    ((PK_SHOTGUN, PK_CHAINGUN, PK_GRENADE), 1248, 1024),
    ((PK_ROCKET, PK_RAIL), 1952, 1024),
]
# the open arena: one spawn in the middle, between the four pillars, that
# is health or a shield (the owner, 2026-11-15)
OPEN_PICKUPS = [((PK_HEALTH, PK_SHIELD), 1024, 1024)]
ARENA_PICKUPS = {"a1": OPEN_PICKUPS, "a2": HEIGHTS_PICKUPS,
                 "a3": HEDGE_PICKUPS, "a4": TWIN_PICKUPS,
                 "a5": CROSS_PICKUPS, "a6": ZIGG_PICKUPS,
                 "a7": PIT_PICKUPS}


def pickups(tag, boxes):
    """An arena's pickups: [(kind, x, y, z, choices)], z the floor under it,
    choices a bit a kind it may spawn as (0: always kind). Refused in a tall
    box, outside the room, past WA_MPK, or choosing among anything but
    weapons"""
    boxes = [bx(b) for b in boxes]
    out = []
    for k, x, y in ARENA_PICKUPS.get(tag, []):
        if not (ROOM[0] < x < ROOM[2] and ROOM[1] < y < ROOM[3]) or \
                any(b[0] < x < b[2] and b[1] < y < b[3] and
                    ((b.h >= hmax(boxes) and not climbed(b, boxes)) or
                     b.kind == LINE) for b in boxes) or not in_walk(tag, x, y):
            raise SystemExit("arenagen: arena %s: a pickup at (%d, %d) is in a "
                             "wall, or outside its floor lines" % (tag, x, y))
        ks = k if isinstance(k, tuple) else (k,)
        if len(ks) > 1 and max(ks) >= PK_HEALTH and \
                set(ks) != {PK_HEALTH, PK_SHIELD}:
            raise SystemExit("arenagen: arena %s: a spawn at (%d, %d) picks "
                             "among more than weapons, or health and shield "
                             "(wa_pkroll takes any choices; the owner's rule "
                             "keeps the two kinds apart)" % (tag, x, y))
        out.append((ks[0], x, y, floor_at(x, y, boxes, 0),
                    sum(1 << c for c in ks) if len(ks) > 1 else 0))
    if len(out) > WA_MPK:
        raise SystemExit("arenagen: arena %s has %d pickups - at most %d"
                         % (tag, len(out), WA_MPK))
    return out


def pvs_pickups(tag, boxes, pks):
    """-> per cell [ix][iy] a byte, bit i: pickup i may be seen from the cell
    (the boxes' test: pvs_seen, over the tall boxes, at each pickup's
    PK_PVSR square) - or None, when the arena carries no PVS"""
    boxes = [bx(b) for b in boxes]
    if not pks or pvs_masks(tag, boxes) is None:
        return None
    top = hmax(boxes)
    tall = [i for i, b in enumerate(boxes) if b.h >= top]
    occ = [boxes[i] for i in tall]
    nb = len(boxes)
    targets = boxes + [Box(x - PK_PVSR, y - PK_PVSR, x + PK_PVSR, y + PK_PVSR)
                       for (_, x, y, _, _) in pks]
    have0 = (1 << nb) - 1
    full = (1 << len(pks)) - 1
    g = 1 << PVS_SHIFT
    out, cut = [], 0
    for x0 in range(ROOM[0], ROOM[2], g):
        col = []
        for y0 in range(ROOM[1], ROOM[3], g):
            vis = 0
            for p in pvs_points(x0, y0, x0 + g, y0 + g, occ):
                if vis == full:
                    break
                vis |= pvs_seen(p, targets, tall, have0 | vis << nb) >> nb
            col.append(vis)
            cut += bin(full & ~vis).count("1")
        out.append(col)
    sys.stderr.write("arenagen: arena %s: the pickups' PVS culls %.1f%% of "
                     "pickup-cells\n" % (tag, 100.0 * cut /
                                          (len(pks) * len(out) * len(out[0]))))
    return out


def pvs_bytes():
    """A cell's mask, bytes: one bit a box, for the largest arena"""
    return (max(len(b) for _, _, b, _ in arenas()) + 7) // 8


# ...and the legs that join PATHS' three loops into one route graph (AI-PLAN
# 3.1): (320, 768)-(320, 1280), (1728, 768)-(1728, 1280), (768, 1280)-(704,
# 800) and (768, 1664)-(320, 1664), by node: loop 0 is 0-3, 1 is 4-7, 2 is 8-13
PATH_LINKS = [(3, 12), (2, 9), (4, 11), (7, 13)]

# --- the dummies' patrols: closed loops of waypoints clear of every block ----
PATHS = [
    [(320, 384), (1728, 384), (1728, 768), (320, 768)],
    [(768, 1280), (1280, 1280), (1280, 1664), (768, 1664)],
    [(1728, 1664), (1728, 1280), (1344, 800), (704, 800), (320, 1280),
     (320, 1664)],
]

# --- the skeleton ------------------------------------------------------------
# joints: 0 head, 1 neck, 2 pelvis, 3 L elbow, 4 L hand, 5 R elbow, 6 R hand,
#         7 L knee, 8 L foot, 9 R knee, 10 R foot
# local frame: x right, y up, z forward (metres)
NJ = 11
assert NJ == 11, "wrender.inc's WA_LIMB calls and WA_MINMAX are written for 11"
THIGH, SHIN, UARM, FARM = 0.46, 0.46, 0.30, 0.28


def rot_x(p, about, ang):
    """Rotate point p round the x axis through `about` by ang (radians);
    positive tips +y toward +z (forward)."""
    x, y, z = p
    ay, az = about[1], about[2]
    dy, dz = y - ay, z - az
    c, s = math.cos(ang), math.sin(ang)
    return (x, ay + dy * c + dz * s, az - dy * s + dz * c)


def limb(base, ang, length):
    """A limb from base hanging DOWN, swung forward by ang (radians)."""
    return (base[0], base[1] - length * math.cos(ang),
            base[2] + length * math.sin(ang))


def pose(hipL, kneeL, hipR, kneeR, shL, elL, shR, elR, lean=0.0, bob=0.0,
         fall=0.0, sink=0.0):
    """Angles in degrees. hip/shoulder: forward swing of the upper limb;
    knee: backward bend of the shin; elbow: forward bend of the forearm.
    lean tips the torso forward about the pelvis; fall tips the WHOLE body
    backward about the feet's line; sink lowers the pelvis (bent knees)."""
    r = math.radians
    pel = (0.0, 0.96 + bob - sink, 0.0)
    neck = (0.0, pel[1] + 0.52, 0.0)
    head = (0.0, neck[1] + 0.22, 0.0)
    shoulderL = (-0.17, neck[1] - 0.04, 0.0)
    shoulderR = (0.17, neck[1] - 0.04, 0.0)
    eL = limb(shoulderL, r(shL), UARM)
    hL = limb(eL, r(shL + elL), FARM)
    eR = limb(shoulderR, r(shR), UARM)
    hR = limb(eR, r(shR + elR), FARM)
    upper = [head, neck, eL, hL, eR, hR]
    if lean:
        upper = [rot_x(p, pel, r(lean)) for p in upper]
    head, neck, eL, hL, eR, hR = upper
    hipLp = (-0.10, pel[1], 0.0)
    hipRp = (0.10, pel[1], 0.0)
    kL = limb(hipLp, r(hipL), THIGH)
    fL = limb(kL, r(hipL - kneeL), SHIN)
    kR = limb(hipRp, r(hipR), THIGH)
    fR = limb(kR, r(hipR - kneeR), SHIN)
    pts = [head, neck, pel, eL, hL, eR, hR, kL, fL, kR, fR]
    if fall:
        pts = [rot_x(p, (0, 0, 0), -r(fall)) for p in pts]
        pts = on_floor(pts)
    return pts


def on_floor(pts):
    """Nothing sinks through the floor: the whole pose lifted onto it"""
    low = min(p[1] for p in pts)
    if low < 0:
        pts = [(p[0], p[1] - low, p[2]) for p in pts]
    return pts


def run_frame(i, n):
    t = 2 * math.pi * i / n
    sw = math.sin(t)
    return pose(hipL=38 * sw, kneeL=15 + 55 * max(0.0, -math.sin(t + 0.6)),
                hipR=-38 * sw, kneeR=15 + 55 * max(0.0, math.sin(t + 0.6)),
                shL=-35 * sw, elL=75, shR=70, elR=10,   # R arm: the pistol
                lean=10, bob=0.05 * abs(math.cos(t)))


def idle_frame(i, n):
    b = 0.012 * math.sin(2 * math.pi * i / n)
    return pose(4, 6, -4, 6, 6, 15, 80, 5, bob=b)


def flinch_frame(i, n):
    k = [0.6, 1.0, 0.5][i]
    return pose(10 * k, 20 * k, -6 * k, 20 * k, -50 * k, 40, 40 - 60 * k, 30,
                lean=-22 * k, sink=0.06 * k)


def death_frame(i, n):
    k = (i + 1) / n
    e = k * k                   # gathers speed as it goes over
    return pose(25 * k, 70 * k, 10 * k, 90 * k, -60 * k - 20, 30 + 40 * k,
                -40 * k, 50 * k, lean=-15 * k, fall=88 * e, sink=0.3 * k)


def collapse_frame(i, n):
    """The shotgun's kill (WIREARENA-WEAPONS-PLAN 2.3): the knees go and the
    body folds FORWARD onto them - where DEATH goes over backward, straight"""
    k = (i + 1) / n
    over = max(0.0, (k - 0.4) / 0.6)
    return on_floor(pose(hipL=70 * k, kneeL=125 * k, hipR=60 * k, kneeR=135 * k,
                shL=-20 + 110 * k, elL=30 + 50 * k, shR=-10 + 100 * k,
                elR=20 + 60 * k, lean=50 * k, fall=-70 * over * over,
                sink=0.5 * k))         # (the crouch's knees: under it)


def gib_frame(i, n):
    """The rocket's kill (WIREARENA-WEAPONS-PLAN 2.3): every joint flies
    apart from where it stood - outward, up and back down to the floor -
    and the renderer draws the joints alone, the bits. Each joint's
    direction and speed are fixed (a golden-angle spread), so the eleven
    scatter evenly; nothing leaves a signed byte (1.98 m) of the feet"""
    t = (i + 1) / n
    out = []
    for j, (x, y, z) in enumerate(pose(0, 0, 0, 0, 0, 0, 0, 0)):
        a = j * 2.39996 + 0.3
        s = 1.25 * (0.55 + 0.45 * ((j * 37) % 10) / 10.0)
        up = 1.1
        b = y + up - 0.04
        out.append((x + s * math.cos(a) * t,
                    min(JTOP - 0.02, max(0.04, y + up * t - b * t * t)),
                    z + s * math.sin(a) * t))
    return out


def zap_frame(i, n):
    """The railgun's kill, its first half (the owner, 2026-11-14: "struck
    by lightning, then dissolve from head to toe quickly"): stiff and
    arched, the arms flung up, every joint shaking - two keyframes that
    loop for the bolt's half second while the renderer draws it in the
    beam's cyan, flickering white"""
    sw = math.sin(i * 2.1 + 0.4)
    pts = pose(hipL=14, kneeL=4, hipR=-14, kneeR=4,
               shL=118 + 14 * sw, elL=-12, shR=118 - 14 * sw, elR=-12,
               lean=-14, bob=0.03)
    out = []
    for j, (x, y, z) in enumerate(pts):
        out.append((x + 0.035 * math.sin(j * 2.1 + i * 2.7),
                    min(JTOP - 0.02, max(0.02, y + 0.02 * math.sin(j * 1.3
                                                                + i * 1.9))),
                    z + 0.035 * math.cos(j * 1.7 + i * 3.1)))
    return out


def dissolve_frame(i, n):
    """...and its second: drawn as its joints alone (after GIB in ANIMS),
    the zapped body goes from the head down - each keyframe a cut lower,
    every joint above it gone into the foot below it, the rest still
    crackling - until only the feet are left, and then nothing"""
    pts = zap_frame(0, 2)
    top = max(p[1] for p in pts)
    cut = top * (1 - (i + 1) / n) + 0.01     # (a third gone, then two
                                # thirds, then all but the feet: the whole
                                # body is ZAP's last keyframe, just before)
    fL, fR = pts[8], pts[10]
    out = []
    for j, (x, y, z) in enumerate(pts):
        if y > cut:
            out.append(fL if x < 0 else fR)
        else:
            out.append((x + 0.03 * math.sin(j * 2.9 + i * 1.7), y,
                        z + 0.03 * math.cos(j * 2.3 + i * 2.1)))
    return out


HOTANIMS = ("IDLE", "RUN")     # (first in ANIMS: the renderer part's own)
ANIMS = [                       # name, keyframes, builder, loops
    ("IDLE", 4, idle_frame, True),
    ("RUN", 8, run_frame, True),
    ("FLINCH", 3, flinch_frame, False),
    ("DEATH", 8, death_frame, False),
    ("COLLAPSE", 6, collapse_frame, False),
    ("ZAP", 2, zap_frame, True),         # (before GIB: drawn as limbs)
    ("GIB", 6, gib_frame, False),        # (GIB and after: the joints alone,
    ("DISSOLVE", 3, dissolve_frame, False),  # wrender.inc's figures)
]


# The narrow joint path (wrender.inc's WA_FJN) multiplies a joint's height as
# an UNSIGNED byte, and its 16-bit sum holds for heights 0..111 units
# (wa_render's wa_fvn): every keyframe stays inside, or arenagen refuses it.
# (A COLLAPSE that sank two joints 4 below the floor drew them as 252 at
# medium range: two red lines into the sky - WEAPONS-PLAN 3.5)
JMAXY = 111
JTOP = JMAXY / UNIT             # the highest a joint may be, metres

# The joints' order in wa_pose: pose()'s, regrouped so a figure's first
# SEVEN are the ends - head, neck, pelvis, the hands, the feet - and the
# elbows and knees follow (wa_fjl's reduced figure, the J option, reads the
# first seven alone; the renderer's limb pairs follow this order)
JORDER = [0, 1, 2, 4, 6, 8, 10, 3, 5, 7, 9]


def project(pts, k):
    rel = 2 * math.pi * k / NFACE
    c, s = math.cos(rel), math.sin(rel)
    out = []
    for (x, y, z) in pts:
        lat = x * c - z * s
        out.append((int(round(lat * UNIT)), int(round(y * UNIT))))
    return out


def sb(v):
    if not -128 <= v <= 127:
        raise SystemExit("arenagen: %d does not fit a signed byte" % v)
    return v & 0xFF


def angle_of(dx, dy):
    a = math.atan2(dy, dx)
    return int(round(a * 1024 / (2 * math.pi))) & 1023


# --- the BSP (docs/plans/MULTIPLAYER-FPS-PLAN.md section 16, lever 1) --------
# Over BOXES, not segments: a box's faces never hide one another (it is
# convex), so the tree only has to order the boxes, front to back, and the
# room - which encloses everything - is drawn last. Every split is an
# axis-aligned line that separates the boxes WITHOUT cutting one, so the
# guest's node test is one compare and a leaf is one box; an arena with no
# such line is refused here, at build time.
#
# A wall face is one-sided: drawn when the player is on its outer side, and
# p1 -> p2 runs left to right seen from the front. A box's corners are
# c0 (x0,y0), c1 (x1,y0), c2 (x1,y1), c3 (x0,y1); its faces are south c0->c1,
# east c1->c2, north c2->c3, west c3->c0. The room's run the other way.

def box_tree(ids, nodes, BOXES):
    """-> node index. nodes: [kind, box, value, low, high]; kind 0 splits on
    x, 1 on y, 2 is a leaf holding one box."""
    me = len(nodes)
    nodes.append(None)
    if len(ids) == 1:
        nodes[me] = [2, ids[0], 0, -1, -1, ids]
        return me
    best = None
    for axis in (0, 1):
        for v in sorted({BOXES[i][axis] for i in ids} |
                        {BOXES[i][axis + 2] for i in ids}):
            low = [i for i in ids if BOXES[i][axis + 2] <= v]
            high = [i for i in ids if BOXES[i][axis] >= v]
            if low and high and len(low) + len(high) == len(ids):
                score = abs(len(low) - len(high))
                if best is None or score < best[0]:
                    best = (score, axis, v, low, high)
    if best is None:
        raise SystemExit("arenagen: no axis-aligned line separates boxes %r "
                         "without cutting one - move a box" % ids)
    _, axis, v, low, high = best
    lo = box_tree(low, nodes, BOXES)
    hi = box_tree(high, nodes, BOXES)
    nodes[me] = [axis, 0, v, lo, hi, ids]
    return me


def inside(x, y, boxes, r):
    if not (ROOM[0] + r < x < ROOM[2] - r and ROOM[1] + r < y < ROOM[3] - r):
        return True
    return any(a - r < x < c + r and b - r < y < d + r
               for (a, b, c, d) in boxes)


MAXKS = 8          # distinct steps between coordinates (wa_products)


def face_line(b, f):
    """Face f of box b: (axis, value, lo, hi) - the line it lies on (axis 0:
    x = value, 1: y = value) and its extent along it"""
    x0, y0, x1, y1 = b
    return {"S": (1, y0, x0, x1), "N": (1, y1, x0, x1),
            "E": (0, x1, y0, y1), "W": (0, x0, y0, y1)}[f]


def covered(lo, hi, spans):
    """[lo, hi] wholly covered by the union of spans?"""
    at = lo
    for a, b in sorted(spans):
        if a > at:
            return False
        at = max(at, b)
        if at >= hi:
            return True
    return at >= hi


def face_bits(i, boxes):
    """HEIGHTS-PLAN 7.2.1: bits 0-3 face S E N W drawn, 4-7 its top edge.
    A face against the room wall, or wholly covered by neighbours at least
    as tall (a lift counts at its rest height, and its own faces at their
    raised one), is not drawn; a top edge only where neighbours of exactly
    the same height cover it, so the top runs on unbroken."""
    b = boxes[i]
    mine = max(b.h, b.top or 0)
    bits = 0
    for k, f in enumerate(FACES):
        ax, v, lo, hi = face_line(b, f)
        opp = {"S": "N", "N": "S", "E": "W", "W": "E"}[f]
        tall, same = [], []
        for j, n in enumerate(boxes):
            if j == i:
                continue
            nax, nv, nlo, nhi = face_line(n, opp)
            if nax != ax or nv != v or nhi <= lo or nlo >= hi:
                continue
            if n.h >= mine:
                tall.append((nlo, nhi))
            if n.h == b.h and n.kind != LIFT and b.kind not in (LIFT, FLIGHT):
                same.append((nlo, nhi))     # (a ramp's top edge always: its
                                            # high end is what closes the
                                            # slope's columns, HEIGHTS-PLAN
                                            # 7.4.2, as no neighbour's is)
        wall = v in ((ROOM[0], ROOM[2]) if ax == 0 else (ROOM[1], ROOM[3]))
        if not (wall or covered(lo, hi, tall)):
            bits |= 1 << k
        if not covered(lo, hi, same):
            bits |= 16 << k
    if b.kind == LINE:                  # A LINE (ARENAS-PLAN 2): its top's
        bits &= 0xF0                    # edges alone - no face, so no post
                                        # and no floor edge under each: one
                                        # line on the floor, not two
    return bits


def tall_inner(tag, boxes):
    """A TALL box's faces byte is read by the renderer's wa_sil as well as
    the outline path's: a face whose bit is clear (bits 0-3) is left out of
    its silhouette - never drawn, kept or dropped by - because its tall
    neighbours wholly cover it, so it is inside the solid and draws nothing
    (the neighbour, on the player's side of the line they share, is walked
    first). Refused where that is not so: a face covered by a ramp or a lift
    (a slope or a moving top leaves part of it showing), or a tall box with
    a covered face where an eye can rise to the tallest height - there it
    may be drawn as an OUTLINE, whose shut test reads the silhouette's ends
    as the whole box's (HEDGES-PLAN 7.1)"""
    top = hmax(boxes)
    inner = []
    for i, b in enumerate(boxes):
        if b.h < top:
            continue
        bits = face_bits(i, boxes)
        for k, f in enumerate(FACES):
            ax, v, lo, hi = face_line(b, f)
            if bits >> k & 1 or v in ((ROOM[0], ROOM[2]) if ax == 0 else
                                      (ROOM[1], ROOM[3])):
                continue
            inner.append(b.name or str(i))
            opp = {"S": "N", "N": "S", "E": "W", "W": "E"}[f]
            for n in boxes:
                nax, nv, nlo, nhi = face_line(n, opp)
                if n is not b and nax == ax and nv == v and nhi > lo and \
                        nlo < hi and n.h >= top and n.kind != SOLID:
                    raise SystemExit("arenagen: arena %s: %s's %s face is "
                                     "covered by %s, not a solid - wa_sil "
                                     "would leave it out" % (tag, b.name, f,
                                                             n.name))
    if inner and pvs_eyemax(boxes) >= top:
        raise SystemExit("arenagen: arena %s: an eye can rise to the tallest "
                         "height, and %s has a face its neighbours cover - "
                         "wa_sil leaves it out of a box that may be drawn in "
                         "outline" % (tag, inner[0]))


def kind_byte(b, lifts, boxes=()):
    """bit 7: a LINE (the game's alone); bit 6: a TALL box a flight or a lift
    climbs onto - a floor to the player all the same (wa_lists' low tables);
    the renderer reads neither"""
    if b.kind == LINE:
        return SOLID | 0x80
    if b.kind == SOLID and boxes and b.h >= hmax(boxes) and \
            climbed(b, boxes):
        return SOLID | 0x40
    if b.kind == FLIGHT:
        return FLIGHT | FACES.index(b.toward) << 2 | b.treads << 4
    if b.kind == LIFT:
        return LIFT | lifts.index(b) << 4
    k = SOLID                           # A STACK (HEDGES-PLAN 8): bit 2 it
    if boxes:                           # carries the next box, bit 3 it
        i = next(j for j, x in enumerate(boxes) if x is b)  # (stands on
                                        # the one before)
        if i + 1 < len(boxes) and boxes[i + 1].on is b:
            k |= 0x04
        if b.on is not None:
            k |= 0x08
    return k


WA_MSTK = 3                     # boxes a stack may have (wa_lowbox's saves)


def stacks(tag, boxes):
    """A stack's rules (HEDGES-PLAN 8), refused where broken; -> the ROOTS'
    indices, the boxes the tree holds (a stack is its bottom's leaf)"""
    for i, b in enumerate(boxes):
        if b.on is None:
            continue
        a = b.on
        if i == 0 or boxes[i - 1] is not a:
            raise SystemExit("arenagen: arena %s: %s stands on %s, which is "
                             "not just before it" % (tag, b.name, a.name))
        top = hmax(boxes)
        if b.kind != SOLID or a.kind != SOLID or b.h >= top or b.h <= a.h:
            raise SystemExit("arenagen: arena %s: %s on %s - a stack is low "
                             "solids, each taller than the one under it"
                             % (tag, b.name, a.name))
        if not (a[0] < b[0] and b[2] < a[2] and a[1] < b[1] and b[3] < a[3]):
            raise SystemExit("arenagen: arena %s: %s is not strictly inside "
                             "%s, which it stands on" % (tag, b.name, a.name))
        n, c = 1, b
        while c.on is not None:
            n, c = n + 1, c.on
        if n > WA_MSTK:
            raise SystemExit("arenagen: arena %s: a stack of more than %d "
                             "boxes (%s)" % (tag, WA_MSTK, b.name))
    return [i for i, b in enumerate(boxes) if b.on is None]


def stack_root(boxes, i):
    """box i's stack's bottom"""
    b = boxes[i]
    while b.on is not None:
        b = b.on
    return next(j for j, x in enumerate(boxes) if x is b)


def height_checks(tag, boxes):
    """HEIGHTS-PLAN 1.7, 3.1: every rise a walker meets is at most STEPUP,
    a platform no walk reaches is within the jump, a lift's rest is a step
    and its raised height meets a neighbour's top"""
    top = hmax(boxes)
    apex = jump_apex()

    def neighbours(b):
        for f in FACES:
            ax, v, lo, hi = face_line(b, f)
            opp = {"S": "N", "N": "S", "E": "W", "W": "E"}[f]
            for n in boxes:
                nax, nv, nlo, nhi = face_line(n, opp)
                if n is not b and nax == ax and nv == v and nhi > lo and nlo < hi:
                    yield f, n
    def across(b):
        """the boxes facing b over a gap: (box, gap) - parallel faces, their
        stretches overlapping"""
        x0, y0, x1, y1 = b
        for n in boxes:
            if n is b:
                continue
            if n[0] < x1 and x0 < n[2]:
                if n[1] >= y1:
                    yield n, n[1] - y1
                elif n[3] <= y0:
                    yield n, y0 - n[3]
            if n[1] < y1 and y0 < n[3]:
                if n[0] >= x1:
                    yield n, n[0] - x1
                elif n[2] <= x0:
                    yield n, x0 - n[2]
    reach = set()
    for b in boxes:
        if b.kind == LINE:
            continue
        if b.kind == FLIGHT:
            if not 1 <= b.treads <= 15:          # (its nibble, and the
                raise SystemExit("arenagen: arena %s: %s has %d treads - a "
                                 "flight has 1 to 15 (the renderer divides by "
                                 "them)" % (tag, b.name, b.treads))
            rise, odd = divmod(b.h, b.treads)
            if odd or rise > STEPUP:
                raise SystemExit("arenagen: arena %s: %s rises %d/%d a tread "
                                 "- a walk climbs %d" % (tag, b.name, b.h,
                                                         b.treads, STEPUP))
            ends = [n for f, n in neighbours(b) if f == b.toward]
            if not ends or any(not 0 <= n.h - b.h <= STEPUP for n in ends):
                raise SystemExit("arenagen: arena %s: %s does not end a step "
                                 "below what it climbs to" % (tag, b.name))
            reach |= {id(n) for n in ends}
        if b.kind == LIFT:
            if b.h > STEPUP:
                raise SystemExit("arenagen: arena %s: %s rests %d up - not a "
                                 "step" % (tag, b.name, b.h))
            meet = [n for f, n in neighbours(b) if abs(n.h - b.top) <= STEPUP]
            if not meet:
                raise SystemExit("arenagen: arena %s: %s rises to %d, which "
                                 "meets no neighbour's top" % (tag, b.name, b.top))
            reach |= {id(n) for n in meet}
    # ...and a platform a JUMP reaches from a neighbour that is reached -
    # stacked tiers (ARENAS-PLAN 1): within apex - 8 of a reached top beside
    # it, or of the floor
    grow = True
    while grow:
        grow = False
        for b in boxes:
            if b.kind != SOLID or id(b) in reach or b.h <= apex - 8:
                continue
            if any(id(n) in reach and n.kind != LINE and
                   0 <= b.h - max(n.h, n.top or 0) <= apex - 8
                   for f, n in neighbours(b)):
                reach.add(id(b))
                grow = True
            elif any(n.kind != LINE and n.h <= apex - 8 and
                     0 <= b.h - n.h <= apex - 8 for f, n in neighbours(b)):
                reach.add(id(b))
                grow = True
            elif b.on is not None and (id(b.on) in reach or
                                       b.on.h <= apex - 8) and \
                    b.h - b.on.h <= apex - 8:
                reach.add(id(b))        # (a stack's: a jump off the box it
                grow = True             # stands on, HEDGES-PLAN 8)
            elif any(id(n) in reach and n.kind == SOLID and n.h == b.h and
                     0 < gap <= jump_gap() for n, gap in across(b)):
                reach.add(id(b))        # (across a gap a run-up jumps:
                grow = True             # the hedges' outer wall, HEDGES-PLAN)
    for b in boxes:
        if b.kind == SOLID and STEPUP < b.h < top and id(b) not in reach \
                and b.h > apex - 8:
            raise SystemExit("arenagen: arena %s: %s, %d up, is above the "
                             "jump (%d) and no flight or lift reaches it"
                             % (tag, b.name, b.h, apex))


def arena_tables(w, tag, boxes, start):
    """One arena's tables, every label prefixed wa_<tag>_; -> its counts."""
    boxes = [bx(b) for b in boxes]
    if not on_top(start[0], start[1], boxes, 20):     # (a platform's top
                                                        # will do: the Pit's)
        raise SystemExit("arenagen: arena %s starts the player INSIDE a wall "
                         "at %r - every step would collide" % (tag, start))
    if not in_walk(tag, start[0], start[1], 24):
        raise SystemExit("arenagen: arena %s starts the player outside its "
                         "floor lines" % tag)
    height_checks(tag, boxes)
    graph_edges(tag, boxes)             # (the routes' checks: AI-PLAN 3.1)
    nodes = []
    box_tree(stacks(tag, boxes), nodes, boxes)
    # an arena with no room walls (ARENA_ROOMH 0) draws nothing at the
    # room's corners, so they are no vertices and their coordinates none of
    # wa_products': its row of the corner table names vertex 0, unread
    roomless = ARENA_ROOMH.get(tag, WALL_H) == 0
    pts = []
    for (a, b, c, d) in ([] if roomless else [ROOM]) + boxes:
        pts += [(a, b), (c, b), (c, d), (a, d)]
    segs = flines(tag)                  # (a floor line's ends: vertices too,
    if len(segs) > WA_MFL:              # made by the frame's cache)
        raise SystemExit("arenagen: arena %s: %d floor lines - at most %d"
                         % (tag, len(segs), WA_MFL))
    ends = {(x0, y0) for (x0, y0, x1, y1) in segs} | \
           {(x1, y1) for (x0, y0, x1, y1) in segs}
    if len(ends) > WA_MFV:              # (wa_flinit's records: wa_fqvx)
        raise SystemExit("arenagen: arena %s: its floor lines have %d "
                         "distinct ends - at most %d" % (tag, len(ends),
                                                         WA_MFV))
    for (x0, y0, x1, y1) in segs:
        if not (ROOM[0] <= min(x0, x1) and max(x0, x1) <= ROOM[2] and
                ROOM[1] <= min(y0, y1) and max(y0, y1) <= ROOM[3]):
            raise SystemExit("arenagen: arena %s: a floor line outside the "
                             "room" % tag)
        pts += [(x0, y0), (x1, y1)]
    xs = sorted({p[0] for p in pts})
    ys = sorted({p[1] for p in pts})
    verts, vix = [], {}
    for p in pts:
        if p not in vix:
            vix[p] = len(verts)
            verts.append(p)
    if len(verts) * 2 > 255 or len(boxes) * 4 > 255 or len(xs) * 4 > 255 \
            or len(ys) * 4 > 255:             # (a leaf holds its box x 4:
        raise SystemExit("arenagen: arena %s outgrew a byte index: %d "
                         "vertices (127), %d boxes (63), %d x and %d y "
                         "values (63)" % (tag, len(verts), len(boxes),
                                          len(xs), len(ys)))
    # wa_products makes a coordinate's products from the one before it, by
    # adding the products of the STEP between them, made once a frame: so
    # the distinct steps an arena has are few, and each fits x 4 in 16 bits
    steps = {b - a for vals in (xs, ys) for a, b in zip(vals, vals[1:])}
    if len(steps) > MAXKS or max(steps) * 4 > 32767:
        raise SystemExit("arenagen: arena %s: %d distinct coordinate steps, "
                         "largest %d - wa_products takes at most %d, each "
                         "under 8192" % (tag, len(steps), max(steps), MAXKS))
    for (a, b, c, d) in boxes:          # (a box may stand against a wall:
        if not (ROOM[0] <= a < c <= ROOM[2] and ROOM[1] <= b < d <= ROOM[3]):
            raise SystemExit("arenagen: arena %s: a box outside the room - "
                             "WA_ZMAX assumes none is" % tag)
    P = "wa_%s_" % tag
    w(P + "xv:      dw " + ", ".join(str(x) for x in xs))
    w(P + "yv:      dw " + ", ".join(str(y) for y in ys))
    w(P + "vtx:                         ; x index x 4, y index x 4 (byte offsets")
    w("                                ; into the per-frame product tables)")
    for (x, y) in verts:
        w("    db %d, %d" % (xs.index(x) * 4, ys.index(y) * 4))
    w(P + "corn:                        ; per box (the room first): its corners")
    w("                                ; c0..c3 as vertex x 2")
    if roomless:
        w("    db 0, 0, 0, 0")
    for (a, b, c, d) in ([] if roomless else [ROOM]) + boxes:
        w("    db " + ", ".join(str(vix[p] * 2) for p in
                                [(a, b), (c, b), (c, d), (a, d)]))
    # THE TREE, rows of two sizes: a LEAF is its kind and its box x 4 (2
    # bytes), a split its kind, 0, its value and its low and high links (8)
    # - wa_order reads no more of either, and follows the links rather than
    # counting rows, so a leaf needs no room for links it has not got
    # (the splits' bounds, which wa_node's view-cone test read, went with
    # it: 2,544 bytes of rows over the eight arenas were 1,036)
    off, at = [], 0
    for nd in nodes:
        off.append(at)
        at += 2 if nd[0] == 2 else 8
    w(P + "bsp:                         ; kind (0 x, 1 y, 2 leaf), box x 4 (a")
    w("                                ; leaf's, into wa_boxes; a split's 0)")
    w("                                ; - and a split's value, low, high:")
    w("                                ; ADDRESSES in the part, rebased by the")
    w("                                ; renderer's ARENA verb")
    for (kind, box, v, lo, hi, ids) in nodes:
        w("    db %d, %d" % (kind, box * 4))
        if kind != 2:
            w("    dw %d, %s, %s" % (v, P + "bsp+%d" % off[lo],
                                  P + "bsp+%d" % off[hi]))
    w(P + "bcol:                        ; per box (the room first): its colour")
    w("                                ; - 0 a boundary wall, 1 an item. The")
    w("                                ; owner's call: colour says WHAT a wall")
    w("                                ; is, not how far (two shades never read")
    w("                                ; as depth, and Hercules has none)")
    w("    db %d" % ARENA_ROOMCOL.get(tag, COL_ROOM) +
      "".join(", %d" % b.col for b in boxes))
    w(P + "boxes:                       ; x0, y0, x1, y1")
    for b in boxes:
        w("    dw %d, %d, %d, %d" % b)
    lifts = [b for b in boxes if b.kind == LIFT]
    if len(lifts) > WA_MLIFT:
        raise SystemExit("arenagen: arena %s has %d lifts - the sim moves %d "
                         "(ARENAS-PLAN 4.1)" % (tag, len(lifts), WA_MLIFT))
    for b in lifts:                     # (the SNAP's byte: height / 4 in six
        if b.h % 4 or b.top % 4 or b.top > 252:     # bits, wa_lksnap)
            raise SystemExit("arenagen: arena %s: lift %s: its heights must "
                             "be multiples of 4, to 252" % (tag, b.name))
    tall_inner(tag, boxes)
    w(P + "bxh:                         ; per box: height (a lift's at rest),")
    w("                                ; kind, faces (HEIGHTS-PLAN 7.2.1)")
    for i, b in enumerate(boxes):
        w("    dw %d" % b.h)
        w("    db %d, %d" % (kind_byte(b, lifts, boxes), face_bits(i, boxes)))
    w(P + "fl:                          ; floor lines: count; x0, y0, x1, y1,")
    w("                                ; its two vertices x 2 (ARENAS-PLAN 4.5)")
    w("    dw %d" % len(segs))
    for (x0, y0, x1, y1) in segs:
        w("    dw %d, %d, %d, %d" % (x0, y0, x1, y1))
        w("    db %d, %d" % (vix[(x0, y0)] * 2, vix[(x1, y1)] * 2))
    w(P + "lifts:                       ; count; box, 0, rest, raised")
    w("    dw %d" % len(lifts))
    for b in lifts:
        w("    db %d, 0" % boxes.index(b))
        w("    dw %d, %d" % (b.h, b.top))
    sys.stderr.write("arenagen: arena %s: %d boxes in a %d-node tree, %d "
                     "vertices, %d x and %d y values\n"
                     % (tag, len(boxes), len(nodes), len(verts), len(xs),
                        len(ys)))
    return len(xs), len(ys), len(verts), len(boxes), at


def bsp_tables(w):
    """Every arena, then their descriptors (SPEC: wa_loadarena copies one into
    the working tables; the tree stays where it is) and the maxima the
    working tables are sized to."""
    counts = []
    w("%ifdef WA_TAB_ARENAS")
    for tag, name, boxes, start in arenas():
        counts.append(arena_tables(w, tag, boxes, start))
    w("%endif")
    w("WA_NARENA equ %d" % len(counts))
    tags = [t[0] for t in arenas()]
    if sorted(ARENA_ORDER) != sorted(tags):
        raise SystemExit("arenagen: ARENA_ORDER names %r, the arenas are %r"
                         % (ARENA_ORDER, tags))
    for i, t in enumerate(ARENA_ORDER):    # (the setup's list: wasetup.inc)
        w("WA_ORD%d equ %d" % (i, tags.index(t)))
        w("WA_ITM%d equ %d" % (tags.index(t), i))
    w("WA_MXV equ %d" % max(c[0] for c in counts))
    w("WA_MYV equ %d" % max(c[1] for c in counts))
    w("WA_MVTX equ %d" % max(c[2] for c in counts))
    w("WA_MBOX equ %d" % max(c[3] for c in counts))
    w("WA_MBSP equ %d" % max(c[4] for c in counts))    # (a tree's bytes)
    w("WA_MAXKS equ %d" % MAXKS)
    w("WA_MPK equ %d" % WA_MPK)
    w("WA_PVSB equ %d" % pvs_bytes())   # (a cell's mask, and the most cells)
    w("WA_PVSN equ %d" % (((ROOM[2] - ROOM[0]) >> PVS_SHIFT) *
                          ((ROOM[3] - ROOM[1]) >> PVS_SHIFT)))
    # the deepest a point can be: the player and every wall are in the room,
    # so no z exceeds its diagonal (wa_rcp covers NEAR..WA_ZMAX-1)
    w("WA_ZMAX equ %d" % (int(math.hypot(ROOM[2] - ROOM[0],
                                         ROOM[3] - ROOM[1])) + 2))
    w("%ifdef WA_TAB_ARENAS")
    w("wa_arenas:                      ; xv, nxv, yv, nyv, vtx, nvtx, corn,")
    w("                                ; boxes, nbox, root, start x, y, angle,")
    w("                                ; colours")
    for (tag, name, boxes, start), (nx, ny, nv, nb, nn) in zip(arenas(), counts):
        P = "wa_%s_" % tag
        w("    dw %sxv, %d, %syv, %d, %svtx, %d, %scorn" % (P, nx, P, ny, P, nv, P))
        w("    dw %sboxes, %d, %sbsp, %d, %d, %d, %sbcol" % (
            P, nb, P, start[0], start[1],
            facing(tag, boxes, *start[:3]), P))
    w("%endif")
    w("WA_ARENASZ equ 28")

def main(out):
    L = []
    w = L.append
    w("; GENERATED by tools/arenagen.py - do not edit (docs/plans/"
      "MULTIPLAYER-FPS-PLAN.md)")
    w("WA_NJ equ %d" % NJ)
    w("WA_NFACE equ %d" % NFACE)
    w("WA_SPEED equ %d" % SPEED)
    w("WA_STEPUP equ %d" % STEPUP)
    w("WA_FRAD equ %d" % FRAD)
    w("WA_ROOM_X0 equ %d" % ROOM[0])
    w("WA_ROOM_Y0 equ %d" % ROOM[1])
    w("WA_ROOM_X1 equ %d" % ROOM[2])
    w("WA_ROOM_Y1 equ %d" % ROOM[3])
    w("%ifdef WA_TAB_SIN")
    w("wa_sin:")                          # a QUARTER wave, 0..256 inclusive:
    for i in range(0, 257, 8):            # wa_sincos mirrors it, and every
        w("    dw " + ", ".join(str(int(round(16384 * math.sin(  # angle comes
            2 * math.pi * j / 1024))))     # out as the whole table's did
            for j in range(i, min(i + 8, 257))))
    w("%endif")
    bsp_tables(w)
    # paths: per dummy, a count and per leg (x, y, heading, steps)
    w("WA_NFIG equ %d" % len(PATHS))
    # an arena's route graph, as its part carries it and the game copies it
    # (graph_block): the most bytes any takes, which the game keeps room for
    w("WA_GPHSZ equ %d" % ((max(len(graph_block(t, bs)) for t, _, bs, _
                                in arenas()) + 1) & ~1))
    w("WA_MMW equ %d" % MM_W)            # (the minimap's samples, and
    w("WA_MMH equ %d" % MM_H)            # the rows it takes: MM_H / 2)
    w("WA_MGN equ %d" % WA_MGN)
    w("%%assign WA_MSTK %d" % WA_MSTK)    # (a stack's boxes: wl_child's saves)
    w("WA_FLR equ %d" % WA_FLR)           # (a flag's reach: CTF-PLAN 2)
    for i, (tag, name, boxes, start) in enumerate(arenas()):
        w("WA_AM%d equ %d" % (i, arena_modes(tag)))  # (MODES-PLAN 6: the
                                        # modes it plays, a bit each)
    # poses
    first = 0
    for name, n, _, _ in ANIMS:
        w("WA_A_%s equ %d" % (name, first))
        w("WA_N_%s equ %d" % (name, n))
        first += n
    w("WA_NFRAMES equ %d" % first)
    w("WA_FRAMESZ equ %d                ; one frame: NFACE x NJ x 2 bytes"
      % (NFACE * NJ * 2))
    # THE HOT KEYFRAMES - the animations a figure is in nearly all the time
    # - stay in the renderer part (WA_TAB_POSE0, read in place); the rest -
    # a flinch and the deaths, a second or so at a time - are the POSE
    # part's (WA_TAB_POSE1), copied in by wa_kfrows when a figure is in one
    # (WIREARENA-PARTS-PLAN 8.6)
    w("WA_KHOT equ %d                   ; the hot keyframes: %s"
      % (sum(n for name, n, _, _ in ANIMS if name in HOTANIMS),
         ", ".join(HOTANIMS)))
    if [a[0] for a in ANIMS][:len(HOTANIMS)] != list(HOTANIMS):
        raise SystemExit("arenagen: the hot animations lead ANIMS - the "
                         "renderer part's keyframes are one run from 0")
    w("%ifdef WA_TAB_POSE0")
    w("wa_pose0:")
    kbnd = []
    hot = True
    for name, n, fn, loops in ANIMS:
        if hot and name not in HOTANIMS:
            hot = False
            w("%endif")
            w("%ifdef WA_TAB_POSE1")
            w("wa_pose1:")
        frames = []
        for i in range(n):
            pts = fn(i, n)
            frames.append([[pr[j] for j in JORDER]
                           for pr in (project(pts, k) for k in range(NFACE))])
        # wa_fjl's narrow path blends a joint between two keyframes in
        # BYTES: B - A must be a signed byte for each coordinate between
        # every pair it can blend - the next keyframe, and the first after
        # the last when it loops (one that holds blends its last with itself)
        for i in range(n):
            for k in range(NFACE):
                for (a, b) in frames[i][k]:
                    if not 0 <= b <= JMAXY:
                        raise SystemExit("arenagen: %s %d facing %d: a "
                                         "joint %d units up - the narrow "
                                         "path takes 0..%d (WA_FJN)"
                                         % (name, i, k, b, JMAXY))
        for i in range(n if loops else n - 1):
            for k in range(NFACE):
                for (a, b), (c, d) in zip(frames[i][k],
                                          frames[(i + 1) % n][k]):
                    if not (-128 <= c - a <= 127 and -128 <= d - b <= 127):
                        raise SystemExit("arenagen: %s %d facing %d: a "
                                         "joint moves past a signed byte "
                                         "(wa_fjl's blend)" % (name, i, k))
        for i in range(n):
            for k in range(NFACE):
                w("    db " + ", ".join("%d,%d" % (sb(a), sb(b))
                                         for a, b in frames[i][k]) +
                  "   ; %s %d facing %d" % (name, i, k))
        # wa_ktop and wa_krch (wa_figure's box, made BEFORE the joints are):
        # per keyframe the highest any joint is, and - per CLASS of facing,
        # the angle between the figure's facing and the view folded into a
        # quarter turn (facings k, NFACE/2 - k, NFACE/2 + k and NFACE - k
        # share one: a pose is nearly its own mirror, and the most of the
        # four is taken) - how far any joint reaches to either side of the
        # feet's line. Each is taken over the keyframe AND the one it can
        # blend toward (the next; the first after the last, looping or
        # not), since a blend lies between its two
        half = NFACE // 2
        def cls(k):
            return min(k % half, half - k % half)
        for i in range(n):
            fr = [frames[j] for j in {i, min(i + 1, n - 1), (i + 1) % n}]
            top = max(b for f in fr for fk in f for a, b in fk)
            rch = [max(abs(a) for f in fr for k in range(NFACE)
                       if cls(k) == c for a, b in f[k])
                   for c in range(half // 2 + 1)]
            if max(rch) > 127 or top > JMAXY:
                raise SystemExit("arenagen: %s %d: a bound past a byte" % (name, i))
            kbnd.append((top, rch, "%s %d" % (name, i)))
    w("%endif")                         # (the poses: their own part)
    w("%ifdef WA_TAB_KBND")
    w("wa_ktop:                         ; per keyframe: its joints' top...")
    w("    db " + ", ".join("%d" % t for t, _, _ in kbnd))
    w("wa_krch:                         ; ...and per facing class, per")
    for c in range(NFACE // 4 + 1):     # keyframe, their reach to either side
        w("    db " + ", ".join("%d" % r[c] for _, r, _ in kbnd) +
          "   ; class %d" % c)
    w("%endif")
    # Every constant first, once: an includer that wants the constants
    # before its own layout and the data blocks later (the renderer part,
    # apps/wirearena/wrpart.asm) includes this file twice, and the second
    # pass defines only the data it asks for
    const = [l for l in L[1:] if re.match(r"^WA_\w+ equ ", l)]
    rest = [l for l in L[1:] if not re.match(r"^WA_\w+ equ ", l)]
    L = L[:1] + ["%ifndef WATAB_CONSTS", "%define WATAB_CONSTS"] + const + \
        ["%endif"] + rest
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")


def path_block(paths, boxes):
    """An arena's own patrols as its part carries them (HEIGHTS-PLAN 7.4.5):
    WA_NFIG words, each path's offset in the block, then per path a leg
    count and per leg (x, y, heading, steps) - the primary's wa_pathN rows,
    which wa_spawn and wa_figtick walk. -> the block's words"""
    if len(paths) != len(PATHS):
        raise SystemExit("arenagen: an arena's own patrols must be %d (WA_NFIG)"
                         % len(PATHS))
    words, offs = [], []
    at = 2 * len(paths)
    for p in paths:
        offs.append(at)
        words.append(len(p))
        for j, (x, y) in enumerate(p):
            nx, ny = p[(j + 1) % len(p)]
            dx, dy = nx - x, ny - y
            steps = max(1, int(round(math.hypot(dx, dy) / SPEED)))
            # bit 15 of the heading: the leg leaves the ground somewhere
            # (wa_figz reads the floor only on such a leg)
            up = any(floor_at(x + dx * k / steps, y + dy * k / steps,
                              boxes, FRAD) for k in range(steps + 1))
            words += [x, y, angle_of(dx, dy) | (0x8000 if up else 0), steps]
        at = 2 * len(paths) + 2 * len(words)
    return offs + words


def arena_part(n, out):
    """Arena N as an OP_ASSET part of WIRERENA.O88, assembled at org 0
    (docs/plans/completed/WIREARENA-PARTS-PLAN.md 7.5): wa_arenas' descriptor row at
    offset 0 - every table offset relative to the part, the tree's links too
    - plus the tree's length in bytes, then the tables it names."""
    tag, name, boxes, start = arenas()[n]
    T = []
    nx, ny, nv, nb, nn = arena_tables(T.append, tag, boxes, start)
    import wamodel                      # (it reads the arenas from here)
    (kb, kp), (ke, ep) = wamodel.budget(n)
    ob, oe = OVER_BUDGET.get(tag, (wamodel.BUDGET_BOXES,
                                   wamodel.BUDGET_EDGES))
    sys.stderr.write("arenagen: arena %s: at worst %d boxes in view at %r, "
                     "%d outline edges at %r\n" % (tag, kb, kp, ke, ep))
    if kb > ob or ke > oe:
        raise SystemExit("arenagen: arena %s is over the visible budget "
                         "(docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 4.2): %d boxes "
                         "in view at %r, %d outline edges at %r - at most %d "
                         "and %d" % (tag, kb, kp, ke, ep, wamodel.BUDGET_BOXES,
                                     wamodel.BUDGET_EDGES))
    P = "wa_%s_" % tag
    L = ["; GENERATED by tools/arenagen.py - do not edit: arena %d (%s), an "
         "OP_ASSET part" % (n, name),
         "; of WIRERENA.O88 (docs/plans/completed/WIREARENA-PARTS-PLAN.md 7.5)",
         "    cpu 8086", "    bits 16", "    org 0",
         "    dw %sxv, %d, %syv, %d, %svtx, %d, %scorn" % (P, nx, P, ny, P, nv,
                                                         P),
         "    dw %sboxes, %d, %sbsp, %d, %d, %d, %sbcol, %d" % (
             P, nb, P, start[0], start[1], facing(tag, boxes, *start[:3]),
             P, nn),
         "    dw %sbxh, %d, %d, %slifts" % (P, ARENA_ROOMH.get(tag, WALL_H),
                                         hmax(boxes), P)]
    gb = graph_block(tag, boxes)
    L += ["    dw %s, %d                ; its route graph: offset, bytes "
          "(AI-PLAN 3.1)" % (P + "graph", len(gb))]
    pv = wamodel.A.pvs_masks(tag, boxes)    # (the budget's walk made them -
    _PVS[tag] = pv                          # in wamodel's copy of this module:
                                            # ours takes them, not makes them)
    L += ["    dw %s                    ; its PVS (0: none): grid shift, cells "
          "a column, masks" % (P + "pvs" if pv else "0")]
    pk = pickups(tag, boxes)
    pkv = pvs_pickups(tag, boxes, pk)
    L += ["    dw %s, %s               ; its pickups (0: none), and their PVS "
          "bytes (0: none - every pickup tested)" % (
              P + "pks" if pk else "0", P + "pkv" if pkv else "0"),
          "    dw %s                    ; its floor lines (ARENAS-PLAN 4.5)" % (
              P + "fl")] + T
    if pk:
        L.append(P + "pks:                     ; count; x, y, z, kind, choices")
        L.append("    dw %d" % len(pk))
        for (k, x, y, z, ch) in pk:
            L.append("    dw %d, %d, %d" % (x, y, z))
            L.append("    db %d, %d" % (k, ch))
    if pkv:
        L.append(P + "pkv:                     ; a byte a PVS cell, column-major")
        for col in pkv:
            L.append("    db " + ", ".join(str(m) for m in col))
    if pv:
        nb_ = pvs_bytes()
        L.append(P + "pvs:")
        L.append("    db %d, %d" % (PVS_SHIFT, len(pv[0])))
        L.append("    dw %d" % (len(pv) * len(pv[0]) * nb_))
        for col in pv:
            L.append("    db " + ", ".join(str(m >> (8 * k) & 255) for m in col
                                           for k in range(nb_)))
    L.append(P + "graph:")
    for i in range(0, len(gb), 12):
        L.append("    db " + ", ".join(str(v) for v in gb[i:i + 12]))
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--arena":
        arena_part(int(sys.argv[2]), sys.argv[3])
    elif len(sys.argv) == 2:
        main(sys.argv[1])
    else:
        raise SystemExit(__doc__)
