#!/usr/bin/env python3
"""PIXELSTEIN 3D's levels (SPEC.md 97.7): text in, a checked stream out.

    python3 tools/pxslevel.py [-o apps/pixelstein/pxlev.inc] [--stream build/pxslev.bin]
                              [--check] [--sweep] [levels/*.txt ...]

One character a cell, in apps/pixelstein/levels/*.txt (the LEGEND is below),
and the tool is where a level is REFUSED: every rule the engine's arithmetic
rests on is checked here, on the host, in a fraction of a second, because a
screendump cannot show a stranded key or a 30-cell sight line and the machine
that would show one is a 4.77 MHz 8088 four boots away.

  reachability   every open cell is reachable from the spawn, and every key
                 before the door it opens - a stranded pickup is a floor that
                 never clears (apps/dotdel's t_ddmaze made the same argument
                 about a stranded dot)
  the counts     <= 64 doors, <= 32 actors, <= 96 statics: the engine's
                 tables (97.9)
  sight lines    no axial run of open cells longer than 24: a longer one is
                 a DDA walk the frame table was not priced for, and a
                 present that copies the whole view
  the DDA budget every open cell x 16 headings, one ray each, through the
                 SAME walker tools/pxssim.py renders with: mean <= 12
                 crossings, worst <= 26 (97.1's table is priced at 10) -
                 cast TWICE, with every door shut and with every door open,
                 and held to the worse state (a door in front of the player
                 is open more often than not, and that ray is the long one)
  melee          no open cell has more than two actors (guards and, since
                 wave 6, dogs) within 1.5 tiles of it
                 at spawn: a third at melee is the frame the sprite cap
                 exists for (97.6). tests/unit/t_pxsmap.py is the row that
                 holds it (wave 3), with a three-guard level as its control

The STREAM (--stream) is what the package's lazy level part carries (97.9):
one record a level, run-length coded, and pxlev.inc is only the DIRECTORY -
offsets, lengths, counts - so the include holds no level bytes and stays a
text file a diff can read. Both are functions of the text files alone;
tests/unit/t_pxsgen.py regenerates the include on every `make`.

THE CELL BYTE (97.1): high nibble = material 1..15 (0 = open), low nibble =
flags - bit 0 SOLID, bit 1 DOOR, bit 2 DOOR_EW (the slab runs east-west, so
the corridor through it runs north-south), bit 3 SPECIAL (the elevator switch
on a solid cell; a secret door on a door cell). Material 15 is the jamb and is
never written in a level: the engine decides it AT HIT TIME (97.2.4) - a
solid face reached through a door cell takes it, and nothing beside the
door is painted with it (the "two cells beside a door" rule of the first
draft is withdrawn, PIXELSTEIN-PLAN 13's fifth graft).
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pxstab                                               # noqa: E402

DEFAULT_OUT = os.path.join(ROOT, "apps", "pixelstein", "pxlev.inc")
DEFAULT_DIR = os.path.join(ROOT, "apps", "pixelstein", "levels")

MAP_W = MAP_H = 64
SOLID, DOOR, DOOR_EW, SPECIAL = 1, 2, 4, 8
MAX_DOORS, MAX_ACTORS, MAX_STATICS = 64, 32, 96
MAX_SIGHT = 24
DDA_MEAN, DDA_WORST = 12.0, 26
MELEE_R2 = 1.5 * 1.5

# --- the legend ---------------------------------------------------------------
# material index, and the material NAMES are the theme's (docs/plans/
# PIXELSTEIN-PLAN.md 12.3): a castle of grey and blue stone, wood, brick and
# iron. The placeholder set; wave 2 names the real fifteen from --preview.
MATERIALS = {
    "#": 1,   # grey stone
    "%": 2,   # grey stone, the second cut
    "B": 3,   # blue stone
    "W": 4,   # wood panelling
    "w": 5,   # wood, the second cut
    "b": 6,   # brick
    "F": 7,   # banner over stone
    "P": 8,   # portrait over wood
    "E": 9,   # emblem over stone
    "C": 10,  # cell door (a solid wall with bars drawn on it)
    "c": 11,  # cell bars
    "S": 12,  # steel door (solid: a door that never opens)
    "L": 13,  # elevator, USED - what the switch becomes
    "X": 14,  # elevator switch (SPECIAL on a solid cell: Use ends the floor)
}
JAMB = 15
MAT_NAMES = {1: "grey stone", 2: "grey stone 2", 3: "blue stone", 4: "wood",
             5: "wood 2", 6: "brick", 7: "banner", 8: "portrait", 9: "emblem",
             10: "cell door", 11: "cell bars", 12: "steel door",
             13: "elevator used", 14: "elevator switch", 15: "jamb"}
DOOR_MAT = 12                   # a door slab is drawn with the steel door
SECRET = "s"                    # a secret door: SPECIAL on a door cell, the
                                # wall's own material either side
DOORS = {"D": 0, "1": 1, "2": 2}    # unlocked, gold lock, silver lock
SPAWNS = {"@": 0, ">": 0, "v": 1024, "<": 2048, "^": 3072}
ACTORS = {"g": 0, "h": 1,           # guard, hound (dog) - STANDING until
          "G": 0x80, "H": 0x81}     # they see the player; the capitals PATROL
                                    # (bit 7 of the kind byte, wave 3: a
                                    # patroller walks its facing and turns at
                                    # a wall; the engine's px_act_patrol)
PATROL = 0x80
PICKUPS = {"a": 0, "m": 1, "f": 2, "k": 3, "K": 4, "t": 5, "T": 6, "e": 7}
#   ammo, medkit, food, GOLD key, SILVER key, treasure, chalice, extra life
DECOR = {"*": (8, True), "&": (9, True), "$": (10, True),
         ":": (11, False), ",": (12, False), ";": (13, False)}
#   pillar, barrel, table (blocking); bones, puddle, plant (walk-through)
KEY_FOR_LOCK = {1: 3, 2: 4}         # lock -> the pickup that opens it


class LevelError(Exception):
    pass


# THE FLOOR'S PASSWORD (wave 4, SPEC.md 97.13): a comment line
# `# code: ABCD` in the level file - four capital letters, one per floor,
# no two alike - which the LEVELDONE card shows for the NEXT floor and the
# attract page's C key takes, instead of a save file (PIXELSTEIN-PLAN 12.5)
CODE_RE = re.compile(r"^# code: ([A-Z]{4})\s*$")


class Level:
    def __init__(self, name):
        self.name = name
        self.cells = bytearray(MAP_W * MAP_H)
        self.spawn = None           # (x, y, angle)
        self.doors = []             # (x, y, flags, lock)
        self.actors = []            # (x, y, kind, facing)
        self.statics = []           # (x, y, kind, blocking)
        self.code = None            # the floor's password (97.13)

    def at(self, x, y):
        return self.cells[y * MAP_W + x]

    def solid(self, x, y):
        return self.at(x, y) & SOLID

    def open_(self, x, y):
        return not self.solid(x, y)


def parse(path):
    """The text file to a Level, or LevelError naming the cell."""
    name = os.path.splitext(os.path.basename(path))[0].upper()
    lv = Level(name)
    # A COMMENT is '# ' (hash, space) or a bare '#': a map row never holds a
    # space, because space is not in the legend, and a border row is '#...#'.
    rows = []
    for ln in open(path):
        ln = ln.rstrip("\n")
        m = CODE_RE.match(ln)
        if m:
            if lv.code:
                raise LevelError("%s: two codes, %s and %s" % (name, lv.code, m.group(1)))
            lv.code = m.group(1)
            continue
        if ln.startswith("# ") or ln == "#" or not ln.strip():
            continue
        rows.append(ln)
    if len(rows) > MAP_H:
        raise LevelError("%s: %d rows, the map is %d" % (name, len(rows), MAP_H))
    grid = []
    for r in rows:
        if len(r) > MAP_W:
            raise LevelError("%s: a row of %d, the map is %d" % (name, len(r), MAP_W))
        grid.append(r + "#" * (MAP_W - len(r)))
    while len(grid) < MAP_H:
        grid.append("#" * MAP_W)
    pending_doors = []
    for y in range(MAP_H):
        for x in range(MAP_W):
            ch = grid[y][x]
            here = (name, x, y, ch)
            if ch == ".":
                v = 0
            elif ch in MATERIALS:
                m = MATERIALS[ch]
                v = (m << 4) | SOLID | (SPECIAL if m == MATERIALS["X"] else 0)
            elif ch in DOORS or ch == SECRET:
                v = 0                       # filled in once the neighbours are known
                pending_doors.append((x, y, ch))
            elif ch in SPAWNS:
                if lv.spawn:
                    raise LevelError("%s: two spawns, at %s and (%d,%d)"
                                     % (name, lv.spawn[:2], x, y))
                lv.spawn = (x, y, SPAWNS[ch])
                v = 0
            elif ch in ACTORS:
                lv.actors.append([x, y, ACTORS[ch], 1024])
                v = 0
            elif ch in PICKUPS:
                lv.statics.append((x, y, PICKUPS[ch], False))
                v = 0
            elif ch in DECOR:
                k, blk = DECOR[ch]
                lv.statics.append((x, y, k, blk))
                v = 0
            else:
                raise LevelError("%s: (%d,%d) is %r, which the legend does not name"
                                 % (name, x, y, ch))
            lv.cells[y * MAP_W + x] = v
    # the border must be solid: the DDA walks off the map otherwise
    for x in range(MAP_W):
        for y in (0, MAP_H - 1):
            if not lv.solid(x, y):
                raise LevelError("%s: the border is open at (%d,%d)" % (name, x, y))
    for y in range(MAP_H):
        for x in (0, MAP_W - 1):
            if not lv.solid(x, y):
                raise LevelError("%s: the border is open at (%d,%d)" % (name, x, y))
    # doors: orientation from the solid pair either side
    for x, y, ch in pending_doors:
        ns = lv.solid(x, y - 1) and lv.solid(x, y + 1)
        ew = lv.solid(x - 1, y) and lv.solid(x + 1, y)
        if ns == ew:
            raise LevelError("%s: the door at (%d,%d) does not sit in a wall "
                             "(solid on exactly one axis)" % (name, x, y))
        # ...and opens onto open cells BOTH ways along its passage axis
        # (review r1: E1M1 had three doors, its only locked one among them,
        # whose far side was a wall - a double wall the door could not pass)
        for px, py in (((x - 1, y), (x + 1, y)) if ns else ((x, y - 1), (x, y + 1))):
            if lv.solid(px, py):
                raise LevelError("%s: the door at (%d,%d) opens into a wall at (%d,%d)"
                                 % (name, x, y, px, py))
        flags = DOOR | (DOOR_EW if ew else 0)
        if ch == SECRET:
            # the wall's own material, taken from the solid neighbour
            nb = lv.at(x - 1, y) if ew else lv.at(x, y - 1)
            mat = nb >> 4
            flags |= SPECIAL
            lock = 0
        else:
            mat = DOOR_MAT
            lock = DOORS[ch]
        lv.cells[y * MAP_W + x] = (mat << 4) | flags
        lv.doors.append((x, y, flags, lock))
    if not lv.spawn:
        raise LevelError("%s: no spawn (@ > < ^ v)" % name)
    # a guard faces the longest open run from its cell
    for a in lv.actors:
        best, bestd = -1, 0
        for ang, (dx, dy) in ((0, (1, 0)), (1024, (0, 1)), (2048, (-1, 0)),
                              (3072, (0, -1))):
            n, cx, cy = 0, a[0] + dx, a[1] + dy
            while 0 <= cx < MAP_W and 0 <= cy < MAP_H and lv.open_(cx, cy) \
                    and not (lv.at(cx, cy) & DOOR):
                n += 1
                cx += dx
                cy += dy
            if n > bestd:
                best, bestd = ang, n
        a[3] = best if best >= 0 else 1024
    return lv


# --- the checks ---------------------------------------------------------------

def check_counts(lv, bad):
    if len(lv.doors) > MAX_DOORS:
        bad.append("%d doors, the table holds %d" % (len(lv.doors), MAX_DOORS))
    if len(lv.actors) > MAX_ACTORS:
        bad.append("%d actors, the table holds %d" % (len(lv.actors), MAX_ACTORS))
    if len(lv.statics) > MAX_STATICS:
        bad.append("%d statics, the table holds %d" % (len(lv.statics), MAX_STATICS))


def check_reach(lv, bad):
    """Flood from the spawn; a locked door opens once its key is reached."""
    blocking = set((s[0], s[1]) for s in lv.statics if s[3])
    keys_at = {}
    for s in lv.statics:
        if s[2] in (3, 4):
            keys_at[(s[0], s[1])] = s[2]
    lock_at = {(d[0], d[1]): d[3] for d in lv.doors}
    have = set()
    seen = set()
    while True:
        seen = set()
        stack = [lv.spawn[:2]]
        while stack:
            x, y = stack.pop()
            if (x, y) in seen:
                continue
            seen.add((x, y))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if lv.solid(nx, ny) or (nx, ny) in blocking:
                    continue
                lock = lock_at.get((nx, ny), 0)
                if lock and KEY_FOR_LOCK[lock] not in have:
                    continue
                stack.append((nx, ny))
        got = set(keys_at[c] for c in seen if c in keys_at)
        if got <= have:
            break
        have |= got
    for y in range(MAP_H):
        for x in range(MAP_W):
            if lv.open_(x, y) and (x, y) not in seen and (x, y) not in blocking:
                bad.append("(%d,%d) is open and unreachable from the spawn "
                           "(a key behind its own door counts)" % (x, y))
                return
    # the elevator switch must have a reachable cell beside it
    for y in range(MAP_H):
        for x in range(MAP_W):
            if lv.at(x, y) >> 4 == 14:
                if not any((x + dx, y + dy) in seen
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    bad.append("the elevator switch at (%d,%d) cannot be reached"
                               % (x, y))
    if not any(lv.at(x, y) >> 4 == 14 for y in range(MAP_H) for x in range(MAP_W)):
        bad.append("no elevator switch (X): the floor cannot end")


def check_sight(lv, bad):
    """No axial run of open cells longer than MAX_SIGHT. Doors count as open:
    they open."""
    worst = (0, None)
    for y in range(MAP_H):
        run, x0 = 0, 0
        for x in range(MAP_W):
            if lv.open_(x, y):
                run += 1
                if run > worst[0]:
                    worst = (run, ((x0, y), "east"))
            else:
                run, x0 = 0, x + 1
    for x in range(MAP_W):
        run, y0 = 0, 0
        for y in range(MAP_H):
            if lv.open_(x, y):
                run += 1
                if run > worst[0]:
                    worst = (run, ((x, y0), "south"))
            else:
                run, y0 = 0, y + 1
    if worst[0] > MAX_SIGHT:
        bad.append("a sight line of %d cells from %s running %s; the rule is %d"
                   % (worst[0], worst[1][0], worst[1][1], MAX_SIGHT))
    return worst[0]


def check_melee(lv, bad):
    """No open cell with more than two ACTORS within 1.5 tiles at spawn -
    guards and dogs, standing or patrolling (the kind byte's low bits name
    the guard or the dog, bit 7 the patrol). What the rule bounds is the
    frame the sprite cap exists for (97.6): three sprites at melee, and a
    dog at the player's elbow is as tall a sprite as a guard is - so the
    dog counts (wave 6: wave 3's rule left it out until it existed)."""
    guards = [(a[0] + 0.5, a[1] + 0.5) for a in lv.actors]
    for y in range(MAP_H):
        for x in range(MAP_W):
            if not lv.open_(x, y):
                continue
            cx, cy = x + 0.5, y + 0.5
            n = sum(1 for gx, gy in guards if (gx - cx) ** 2 + (gy - cy) ** 2 <= MELEE_R2)
            if n > 2:
                bad.append("(%d,%d) has %d actors within melee reach; the sprite "
                           "cap allows two" % (x, y, n))
                return


def sweep_dda(cells):
    """Every open cell x 16 headings, one ray each, through pxssim's walker:
    (mean, worst, (x, y, heading) of the worst)."""
    import pxssim
    total, n, worst, where = 0, 0, 0, None
    for y in range(MAP_H):
        for x in range(MAP_W):
            if cells[y * MAP_W + x] & SOLID:
                continue
            px, py = x * 256 + 128, y * 256 + 128
            for h in range(16):
                a = h * (pxstab.ANG // 16)
                r = pxssim.cast_ray(cells, px, py, a)
                c = r["crossings"]
                total += c
                n += 1
                if c > worst:
                    worst, where = c, (x, y, a)
    return (total / float(n) if n else 0.0), worst, where


def check_dda(lv, bad, full=False):
    """The DDA budget in BOTH door states, and the worse of the two counts.

    A closed door stops the walker (cell & DOOR is a hit, 97.2.4); an open
    one is walked through, so the ray that crosses a doorway into the next
    room is the long one, and the game spends most of its frames with the
    door in front of the player OPEN - that is what a door is for. The first
    cut of this sweep cast with every door shut and E1M1 read worst 23; the
    same level with its 22 doors open read 26, the budget exactly, and no
    edit that added one crossing behind an open door would have failed here.
    check_reach and check_sight already treat a door as open (a locked one
    opens once its key is found); this brings the sweep into line with them.
    Reported: the worse mean and the worse worst, with the state named.
    """
    closed = sweep_dda(lv.cells)
    cells = bytearray(lv.cells)
    for x, y, _flags, _lock in lv.doors:
        cells[y * MAP_W + x] = 0
    opened = sweep_dda(cells)
    mean, mstate = max((closed[0], "closed"), (opened[0], "open"))
    (worst, where), wstate = max(((closed[1], closed[2]), "closed"),
                                 ((opened[1], opened[2]), "open"))
    if mean > DDA_MEAN:
        bad.append("the DDA sweep averages %.1f crossings a ray with the doors "
                   "%s; the budget is %.0f (97.1)" % (mean, mstate, DDA_MEAN))
    if worst > DDA_WORST:
        bad.append("the worst ray is %d crossings, from (%d,%d) at %d with the "
                   "doors %s; the budget is %d"
                   % ((worst,) + where + (wstate, DDA_WORST)))
    return mean, worst, where + (wstate,), closed, opened


def check(lv, sweep=True):
    bad = []
    check_counts(lv, bad)
    check_reach(lv, bad)
    sight = check_sight(lv, bad)
    check_melee(lv, bad)
    dda = check_dda(lv, bad) if sweep else None
    return bad, sight, dda


# --- the stream ---------------------------------------------------------------

STREAM_MAGIC = b"PXL\x01"


def rle(cells):
    out = bytearray()
    i = 0
    while i < len(cells):
        v, n = cells[i], 1
        while i + n < len(cells) and cells[i + n] == v and n < 255:
            n += 1
        out += bytes((n, v))
        i += n
    return bytes(out)


def unrle(data, n=MAP_W * MAP_H):
    out = bytearray()
    for i in range(0, len(data), 2):
        out += bytes([data[i + 1]]) * data[i]
    assert len(out) == n
    return bytes(out)


def record(lv):
    """One level: the header, the run-length map, the three tables."""
    m = rle(bytes(lv.cells))
    assert unrle(m) == bytes(lv.cells)
    sx, sy, sa = lv.spawn
    hdr = STREAM_MAGIC + struct.pack("<HHHHBBB", len(m), sx * 256 + 128,
                                     sy * 256 + 128, sa, len(lv.doors),
                                     len(lv.actors), len(lv.statics))
    body = bytearray(m)
    # THE DOORS ARE WRITTEN SORTED BY CELL (wave 3, 97.7): the engine's
    # cell-to-door lookup (px_door_of) walks a per-row start table built at
    # load from this order and refuses a stream whose doors are not sorted,
    # so the parser's row-major walk is a contract and this is where it is
    # asserted rather than believed
    cells = [y * MAP_W + x for x, y, _f, _l in lv.doors]
    assert cells == sorted(cells), "the doors are not in cell order"
    for x, y, flags, lock in lv.doors:
        body += struct.pack("<HBB", y * MAP_W + x, flags, lock)
    for x, y, kind, facing in lv.actors:
        body += struct.pack("<HBH", y * MAP_W + x, kind, facing)
    for x, y, kind, blocking in lv.statics:
        body += struct.pack("<HBB", y * MAP_W + x, kind, 1 if blocking else 0)
    return bytes(hdr + body)


def generate(levels):
    """(the include text, the stream bytes) for a list of Levels."""
    stream = bytearray()
    dirs = []
    for lv in levels:
        rec = record(lv)
        dirs.append((lv, len(stream), len(rec)))
        stream += rec
    L = []
    w = L.append
    w("; =============================================================================")
    w("; os8088 - apps/pixelstein/pxlev.inc")
    w(";")
    w("; GENERATED by tools/pxslevel.py from apps/pixelstein/levels/*.txt - do not")
    w("; edit by hand (SPEC.md 97.12). tests/unit/t_pxsgen.py regenerates it on")
    w("; every `make`. THE LEVELS ARE NOT IN THIS FILE: they are the run-length")
    w("; stream the same tool writes with --stream, which the package carries as a")
    w("; lazy part (97.9) - this is the DIRECTORY into it, offsets and lengths and")
    w("; counts, so that a level edit is a diff a person can read.")
    w(";")
    w("; The cell byte (97.1): high nibble = material 1..15, 0 open; low nibble")
    w("; bit 0 SOLID, bit 1 DOOR, bit 2 DOOR_EW, bit 3 SPECIAL.")
    w("; =============================================================================")
    w("")
    w("PXC_SOLID   equ %d" % SOLID)
    w("PXC_DOOR    equ %d" % DOOR)
    w("PXC_DOOREW  equ %d              ; the slab runs east-west" % DOOR_EW)
    w("PXC_SPECIAL equ %d" % SPECIAL)
    w("PXM_JAMB    equ %d             ; the material beside a door, never in a level" % JAMB)
    w("PXM_DOOR    equ %d             ; the slab's own" % DOOR_MAT)
    w("PXM_SWITCH  equ %d             ; the elevator switch..." % MATERIALS["X"])
    w("PXM_USED    equ %d             ; ...and what Use turns it into" % MATERIALS["L"])
    w("")
    w("; a level RECORD in the stream: 'PXL',1 then")
    w("PXL_MAPLEN  equ 4               ; word: run-length map bytes")
    w("PXL_SPAWNX  equ 6               ; word: Q8.8 - the tile's centre")
    w("PXL_SPAWNY  equ 8               ; word: Q8.8")
    w("PXL_SPAWNA  equ 10              ; word: the 12-bit heading")
    w("PXL_NDOORS  equ 12              ; byte")
    w("PXL_NACTORS equ 13              ; byte")
    w("PXL_NSTATIC equ 14              ; byte")
    w("PXL_HDR     equ 15              ; ...then the map (count, cell pairs), then")
    w("                                ; doors (cell word, flags, lock), actors")
    w("                                ; (cell word, kind, facing word), statics")
    w("                                ; (cell word, kind, blocking)")
    w("PXL_DOORSZ  equ 4")
    w("PXL_ACTSZ   equ 5")
    w("PXL_STATSZ  equ 4")
    w("PXL_MAXDOORS  equ %d" % MAX_DOORS)
    w("PXL_MAXACTORS equ %d" % MAX_ACTORS)
    w("PXL_MAXSTATIC equ %d" % MAX_STATICS)
    w("PXL_PATROL    equ %d            ; the actor kind byte's bit 7: a patroller" % PATROL)
    w("")
    w("PXL_NLEV    equ %d" % len(levels))
    w("PXL_STREAM  equ %d             ; bytes in the whole stream" % len(stream))
    w("PXL_MAXREC  equ %d             ; ...and its longest record: the claim" % max(d[2] for d in dirs))
    w("")
    w("px_levdir:                       ; per level: word offset, word length,")
    w("                                ; word spawn cell, word spawn heading")
    for lv, ofs, ln in dirs:
        sx, sy, sa = lv.spawn
        w("    dw %d, %d, %d, %d        ; %s: %d doors, %d actors, %d statics"
          % (ofs, ln, sy * MAP_W + sx, sa, lv.name, len(lv.doors),
             len(lv.actors), len(lv.statics)))
    w("PXL_DIRSZ   equ 8")
    w("")
    w("px_levpw:                        ; the floors' passwords, four letters each")
    w("                                ; (97.13: the LEVELDONE card shows the next")
    w("                                ; floor's, the attract page's C key takes one)")
    for lv in levels:
        w("    db '%s'                     ; %s" % (lv.code, lv.name))
    w("")
    return "\n".join(L) + "\n", bytes(stream)


def level_paths(args):
    if args:
        return sorted(args)
    return sorted(os.path.join(DEFAULT_DIR, f) for f in os.listdir(DEFAULT_DIR)
                  if f.endswith(".txt"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("levels", nargs="*")
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--stream", help="write the level stream here")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed include differs")
    ap.add_argument("--no-sweep", action="store_true",
                    help="skip the DDA sweep (the checks that need no cast)")
    a = ap.parse_args()
    levels, failed = [], False
    for p in level_paths(a.levels):
        try:
            lv = parse(p)
        except LevelError as e:
            print("pxslevel: %s" % e)
            failed = True
            continue
        bad, sight, dda = check(lv, sweep=not a.no_sweep)
        print("pxslevel: %s - %d doors, %d actors, %d statics, sight %d%s"
              % (lv.name, len(lv.doors), len(lv.actors), len(lv.statics), sight,
                 (", DDA mean %.1f worst %d at (%d,%d) heading %d doors %s "
                  "(closed %.2f / %d, open %.2f / %d)"
                  % ((dda[0], dda[1]) + dda[2]
                     + (dda[3][0], dda[3][1], dda[4][0], dda[4][1])))
                 if dda else ""))
        for b in bad:
            print("pxslevel:   %s: %s" % (lv.name, b))
            failed = True
        levels.append(lv)
    codes = {}
    for lv in levels:
        if not lv.code:
            print("pxslevel: %s has no `# code: ABCD` line - every floor has a "
                  "four-letter password (97.13)" % lv.name)
            failed = True
        elif lv.code in codes:
            print("pxslevel: %s and %s share the code %s" % (codes[lv.code], lv.name, lv.code))
            failed = True
        else:
            codes[lv.code] = lv.name
    if failed:
        sys.exit(1)
    text, stream = generate(levels)
    if a.check:
        have = open(a.out).read() if os.path.exists(a.out) else ""
        if have != text:
            sys.exit("pxslevel: %s is not what tools/pxslevel.py generates - run "
                     "`python3 tools/pxslevel.py` and commit the result" % a.out)
        print("pxslevel: %s matches" % a.out)
    else:
        open(a.out, "w").write(text)
        print("pxslevel: wrote %s (%d levels)" % (a.out, len(levels)))
    if a.stream:
        os.makedirs(os.path.dirname(a.stream) or ".", exist_ok=True)
        open(a.stream, "wb").write(stream)
        print("pxslevel: wrote %s (%d bytes)" % (a.stream, len(stream)))


if __name__ == "__main__":
    main()
