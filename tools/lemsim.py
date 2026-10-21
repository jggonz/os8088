#!/usr/bin/env python3
"""lemsim: the LEMMINGS mechanics, a second time, in Python (SPEC.md 116.12).

    python3 tools/lemsim.py --fixture DIR --level N --script FILE \\
                            --frames 2000 -o TRACE

THE INDEPENDENT READING the host harness diffs the package against. The C in
apps/lemmings/lemact.c, lemgame.c and lemobj.c is transliterated from Lemmix
(Eric Langedijk, under Lemmix's own licence - zlib-style with an added clause)
src/Game.pas; this file is written from lemmings_3ds
(public domain) src/lemming.c - its structure, its handlers, its object map -
and then held to the rulings SPEC.md 116.4.7 takes where the two references
DISAGREE, each one named at the line that applies it. So a transcription slip
on either side - a sign, an off-by-one, a probe one row out - is a DIVERGENCE
the harness prints, frame and lemming and field, and the build stops; and a
place where the two references genuinely differ is a ruling written down here
rather than a difference nobody noticed. It is the shape tests/unit/t_lemdat.py
gave the converter: a second reader, written from a different source.

THE RULINGS (lemmings_3ds -> Lemmix, because Lemmix models the DOS original's
own ORIG mechanics set and 3ds says "really?" at three of these):

  R1  walking off either end of the world (x < 0 or x > 1647) TURNS AROUND
      (3ds only tests x < 0 and only faces right)
  R2  every terrain probe is HasPixelAt_ClipY: a probe above `miny` reads row
      `miny` (3ds reads the probe row as is)
  R3  the jumper's second probe is ONE ROW HIGHER than its first, because the
      loop moves y between them (3ds re-reads y + 0, "really -1?")
  R4  a faller is removed when its y passes 163 AFTER the step, and a
      bomber's crater is cut and the lemming removed on the explosion's first
      frame (3ds keeps the exploder on the map for 52 frames of particles)
  R5  the basher, miner and builder test x > 1647 for the world's end (3ds
      tests 1584 for the basher)
  R6  the exit takes a splatter or a floater too: only a FALLER is refused
      (3ds's glitch_direct_drop off would refuse both)
  R7  the miner's one-way-right bug and the climber-on-a-shrugger bug are on,
      and objects past the fifteenth non-hatch are inert (Lemmix's ORIG set)
  R8  the 1991 game's three glitches are ON, because decision 1 is the
      original and not Lemmix's defaults (3ds models none): the RIGHT-CLICK
      glitch - the right button held over only working lemmings assigns to the
      last non-prioritised lemming any hit test saw (Lemmix src/Game.pas:3626,
      :3685-3689) - the NUKE glitch, MaxNumLemmings := LemmingsReleased at the
      nuke (:3993), which the finish test and the result count against - and
      the PAUSE glitch (TMechanic.PauseGlitch, Dos.Consts.pas:206-208, "#EL
      2020: finally"): a pause begun before frame 34 holds the CLOCK for
      34 - frame frames once play resumes (BtnPause :3941-3958 sets the count,
      IncrementIteration :3502-3506 spends it instead of a clock frame)
  R9  the level's end WAITS FOR THE LAST SPRAY: HandleExploding sets
      fParticleFinishTimer to PARTICLE_FRAMECOUNT = 52 when ShowParticles is
      on (src/Game.pas:3375-3377; on in TGameOptions DEFAULT, Base.Types.pas
      :161), IncrementIteration spends one a frame (:3508) and
      CheckForGameFinished returns at once while it is above 0 (:2236) - the
      clock's end as well as the all-saved, all-removed and nuke tests. The
      spray itself is pixels and not modelled here; the wait is mechanics,
      because lemmings step and can be saved through it (3ds instead keeps
      the exploder on the map for its 52 frames, R4)

What this file does NOT model, because the harness drives the mechanics only:
the sound, the panel and every
pixel of the picture. The terrain is the MASK, composed from the fixture's
record the way SPEC.md 116.7.1's composer does it.

ATTRIBUTION. Lemmings is (C) 1991 DMA Design / Psygnosis. lemmings_3ds is
public domain (its LICENCE.txt), Lemmix is under its own Licence.txt (zlib's
text with an added clause); nothing here is copied from
either - it is a reading of what they do, with their files cited.
"""
import argparse
import struct
import sys

W, H = 1584, 160
DOM_NONE = 128
PARTICLE_FRAMECOUNT = 52     # R9, Lemmix src/Game.pas:785
DOM_EXIT, DOM_FL, DOM_FR = 129, 130, 131
DOM_WATER, DOM_FIRE, DOM_OWL, DOM_OWR, DOM_STEEL, DOM_BLOCKER = \
    133, 134, 135, 136, 137, 138

# the actions, Lemmix's order (the C's LA_*)
(WALK, JUMP, DIG, CLIMB, DROWN, HOIST, BUILD, BASH, MINE, FALL, FLOAT, SPLAT,
 EXIT, FRY, BLOCK, SHRUG, OHNO, BOMB) = range(18)
FRAMES = [8, 1, 16, 8, 16, 8, 16, 32, 24, 4, 8, 16, 8, 14, 16, 8, 16, 1]
LOOP = [1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0]
FOOTX = [8] * 17 + [16]
FOOTY = [10, 10, 12, 12, 10, 12, 13, 10, 13, 10, 16, 10, 13, 14, 10, 10, 10,
         25]
PRIO = {BLOCK, BUILD, SHRUG, BASH, MINE, DIG, OHNO}
FLOAT_DY = [3, 3, 3, 3, -1, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2]
FLOAT_FR = [1, 2, 3, 5, 5, 5, 5, 5, 5, 6, 7, 7, 6, 5, 4, 4]
SETS = {                      # Lemmix's Assign*'s actionSets
    "blk": {WALK, SHRUG, BUILD, BASH, MINE, DIG},
    "bld": {WALK, SHRUG, BASH, MINE, DIG},
    "bsh": {WALK, SHRUG, BUILD, MINE, DIG},
    "min": {WALK, SHRUG, BUILD, BASH, DIG},
    "dig": {WALK, SHRUG, BUILD, BASH, MINE},
}
MINX, MAXX, MAXY, HEADMINY = 0, 1647, 163, -5


class Lem:
    __slots__ = ("x", "y", "act", "frame", "rtl", "gone", "climb", "float",
                 "block", "newdig", "eoa", "timer", "fallen", "bricks", "fidx",
                 "below", "front", "saved")

    def __init__(self, x, y):
        self.x, self.y = x, y
        self.act, self.frame = FALL, 0
        self.rtl = self.gone = self.climb = self.float = False
        self.block = self.newdig = self.eoa = False
        self.timer, self.fallen, self.bricks, self.fidx = 0, 3, 0, 0
        self.below = self.front = DOM_NONE
        self.saved = [0] * 9

    @property
    def dx(self):
        return -1 if self.rtl else 1


class Fixture:
    """The converted files the package reads, read the way IT reads them."""

    def __init__(self, d):
        rd = lambda n: open("%s/%s" % (d, n), "rb").read()
        self.rat = rd("LEMR0.LEM")
        self.lv = rd("LEMLV0.LEM")
        self.gr = rd("LEMGR0.LEM")
        self.mn = rd("LEMMAIN.LEM")

    def entry(self, n):
        e = self.rat[n * 64:(n + 1) * 64]
        return {
            "rate": struct.unpack_from("<H", e, 4)[0],
            "count": struct.unpack_from("<H", e, 6)[0],
            "save": struct.unpack_from("<H", e, 8)[0],
            "mins": struct.unpack_from("<H", e, 10)[0],
            "skills": list(e[12:20]),
            "sect": e[23],
        }

    def main_item(self, item_id):
        for r in range(16):
            row = self.mn[32 + r * 16:48 + r * 16]
            if row[0] == item_id and struct.unpack_from("<I", row, 12)[0]:
                off = struct.unpack_from("<I", row, 8)[0]
                n = struct.unpack_from("<I", row, 12)[0]
                return self.mn[off:off + n]
        raise SystemExit("lemsim: LEMMAIN has no item %d" % item_id)


class Game:
    def __init__(self, fx, level):
        self.fx = fx
        e = fx.entry(level)
        rec = fx.lv[e["sect"] * 2048:(e["sect"] + 1) * 2048]
        self.mask = [bytearray(W) for _ in range(H)]
        self.compose(rec)
        self.objects(rec)
        self.stencils()
        self.rr = self.rrmin = max(1, min(99, e["rate"]))
        self.maxlem = max(1, min(80, e["count"]))
        self.mins = max(1, min(99, e["mins"]))
        self.secs = 0
        self.cnt = list(e["skills"])
        self.skill = 2
        self.it = self.clockf = 0
        self.released = self.out = self.savedn = self.removed = 0
        self.rrdir = 0
        self.countdown = 20
        self.opened = self.entdone = False
        self.paused = self.nuked = self.nukeing = False
        self.glitchp = 0                       # R8, the pause glitch
        self.pfin = 0                          # R9, fParticleFinishTimer
        self.nukeix = 0
        self.finished = self.timeup = False
        self.lems = []
        self.hitlast = None                    # fLastNonPrioritizedLemming

    # --- the terrain: SPEC.md 116.7.1's composer, the mask half ------------
    def compose(self, rec):
        gr = self.fx.gr
        for i in range(400):
            b0, b1, b2, b3 = rec[0x120 + 4 * i:0x124 + 4 * i]
            if b0 == 0xFF and b1 == 0xFF:
                continue                       # skipped, never a break
            flags = b0 >> 4
            x = (((b0 & 15) << 8) | b1) - 16
            yv = (b2 << 1) | (b3 >> 7)
            y = yv - (516 if yv >= 0x100 else 4)
            pid = b3 & 0x3F
            row = 128 + pid * 8
            w, h = gr[row], gr[row + 1]
            if not w or not h:
                continue
            off = struct.unpack_from("<I", gr, row + 2)[0]
            bpr = (w + 7) // 8
            plane3 = off + 3 * bpr * h
            for gy in range(h):
                dy = y + h - 1 - gy if flags & 4 else y + gy
                if not 0 <= dy < H:
                    continue
                for gx in range(w):
                    px = x + gx
                    if not 0 <= px < W:
                        continue
                    if not (gr[plane3 + gy * bpr + gx // 8] >> (7 - gx % 8)) & 1:
                        continue
                    if flags & 8:              # no-overwrite wins (Lemmix)
                        if not self.mask[dy][px]:
                            self.mask[dy][px] = 1
                    elif flags & 2:
                        self.mask[dy][px] = 0
                    else:
                        self.mask[dy][px] = 1

    # --- the objects and the object map (3ds import_level.c's shape, Lemmix's
    # InitializeObjectMap values) ------------------------------------------
    def objects(self, rec):
        gr = self.fx.gr
        self.types = []
        for t in range(16):
            r = 640 + t * 16
            self.types.append({
                "w": gr[r], "h": gr[r + 1], "frames": max(1, gr[r + 2]),
                "eff": gr[r + 3],
                "tl": struct.unpack_from("<H", gr, r + 4)[0] * 4,
                "tt": struct.unpack_from("<H", gr, r + 6)[0] * 4 - 4,
                "tw": gr[r + 8] * 4, "th": gr[r + 9] * 4,
                "start": gr[r + 11], "anim": gr[896 + t],
            })
        self.objs, self.ents, self.info = [], [], []
        for i in range(32):
            raw = rec[0x20 + 8 * i:0x28 + 8 * i]
            if not any(raw):
                continue
            x = ((raw[0] << 8) | raw[1]) - 16
            y = (raw[2] << 8) | raw[3]
            if y >= 0x8000:
                y -= 0x10000
            t = raw[5] & 15
            o = {"x": x, "y": y, "t": t, "trig": False,
                 "frame": self.types[t]["start"]
                 if self.types[t]["start"] < self.types[t]["frames"] else 0}
            self.objs.append(o)
            if t == 1:
                if len(self.ents) < 4:
                    self.ents.append(o)
            else:
                self.info.append(o)
        n = len(self.ents)
        self.order = {2: [0, 1, 1, 0], 3: [0, 1, 2, 1],
                      4: [0, 1, 2, 3]}.get(n, [0, 0, 0, 0])
        self.omap = [bytearray([DOM_NONE]) * 396 for _ in range(40)]
        for i in range(32):
            b0, b1, b2, b3 = rec[0x760 + 4 * i:0x764 + 4 * i]
            if not (b0 | b1 | b2 | b3):
                continue
            self.ofill(((b0 << 1) + (b1 >> 7)) * 4 - 16, (b1 & 0x7F) * 4,
                       (b2 >> 4) * 4 + 4, (b2 & 15) * 4 + 4, DOM_STEEL)
        for k, o in enumerate(self.info[:16]):          # R7
            t = self.types[o["t"]]
            v = k if t["eff"] == 4 else t["eff"] + 128
            self.ofill((o["x"] & ~3) + t["tl"], (o["y"] & ~3) + t["tt"],
                       t["tw"], t["th"], v)

    def ofill(self, x, y, w, h, v):
        for py in range(y, y + h):
            for px in range(x, x + w):
                self.oset(px, py, v)

    def ocell(self, x, y):
        cx, cy = x >> 2, y >> 2                # Python's >> floors negatives
        if 0 <= cx < 396 and 0 <= cy < 40:
            return cx, cy
        return None

    def oget(self, x, y):
        c = self.ocell(x, y)
        return self.omap[c[1]][c[0]] if c else DOM_NONE

    def oset(self, x, y, v):
        c = self.ocell(x, y)
        if c:
            self.omap[c[1]][c[0]] = v

    # --- the stencils (LEMMAIN items 4 and 9) -----------------------------
    def stencils(self):
        data = self.fx.main_item(4)
        tab = self.fx.main_item(9)
        self.sten = []
        for r in range(6):
            frames, w, h, _bpp, off, per = struct.unpack_from("<BBBBHH", tab,
                                                              r * 8)
            self.sten.append((w, h, [data[off + f * per:off + (f + 1) * per]
                                     for f in range(frames)]))

    def apply(self, row, f, x, y):
        w, h, frames = self.sten[row]
        bits = frames[f]
        bpr = (w + 7) // 8
        for gy in range(h):
            for gx in range(w):
                if (bits[gy * bpr + gx // 8] >> (7 - gx % 8)) & 1:
                    if 0 <= x + gx < W and 0 <= y + gy < H:
                        self.mask[y + gy][x + gx] = 0

    # --- probes ------------------------------------------------------------
    def px(self, x, y):
        return 0 <= x < W and 0 <= y < H and self.mask[y][x] == 1

    def hp(self, x, y, miny):                         # R2
        return self.px(x, max(y, miny))

    # --- transitions --------------------------------------------------------
    def trans(self, L, act, turn=False):
        if L.act == act and not turn:
            return
        if turn:
            L.rtl = not L.rtl
        if L.act == act:
            return
        L.act, L.frame, L.eoa = act, 0, False
        L.fallen = L.bricks = 0
        if act == SPLAT:
            L.timer = 0
        elif act == BLOCK:
            L.block = True
            self.field(L, True)
        elif act == DIG:
            L.newdig = True
        elif act == FALL:
            L.fallen = 3
        elif act == BUILD:
            L.bricks = 12
        elif act == FLOAT:
            L.fidx = 0
        elif act == MINE:
            L.y += 1

    def topcheck(self, L):
        dy = -FOOTY[L.act]
        if L.y + dy < HEADMINY:
            L.y = HEADMINY - 2 - dy
            L.rtl = not L.rtl
            if L.act == JUMP:
                self.trans(L, WALK)

    def field(self, L, on):
        for j, fy in enumerate((-6, -2, 2)):
            for i, fx in enumerate((-4, 0, 4)):
                k = j * 3 + i
                if on:
                    L.saved[k] = self.oget(L.x + fx, L.y + fy)
                    self.oset(L.x + fx, L.y + fy,
                              (DOM_FL, DOM_BLOCKER, DOM_FR)[i])
                else:
                    self.oset(L.x + fx, L.y + fy, L.saved[k])

    def remove(self, L):
        L.gone = True
        self.out -= 1
        self.removed += 1

    def dig_row(self, L, y):
        y = max(y, 0)
        hit = False
        for x in range(L.x - 4, L.x + 5):
            if self.px(x, y):
                self.mask[y][x] = 0
                hit = True
        return hit

    # --- the handlers: lemmings_3ds src/lemming.c's, under the rulings ------
    def handle(self, L):
        if L.act not in (FLOAT, DIG):
            if L.frame < FRAMES[L.act] - 1:
                L.eoa = False
                L.frame += 1
            else:
                L.eoa = True
                if LOOP[L.act]:
                    L.frame = 0
        return getattr(self, "h%d" % L.act)(L)

    def h0(self, L):                                  # walk
        L.x += L.dx
        if not MINX <= L.x <= MAXX:                   # R1
            L.rtl = not L.rtl
            return True
        if self.hp(L.x, L.y, 0):
            i = 1
            while i < 8 and self.hp(L.x, L.y - i, -(i - 1) - 1):
                i += 1
            if i == 8:
                if L.climb:
                    self.trans(L, CLIMB)
                else:
                    L.rtl = not L.rtl
                return True
            if i > 3:
                self.trans(L, JUMP)
                L.y -= 2
            else:
                L.y -= i - 1
            self.topcheck(L)
            return True
        i = 1
        while i < 4:
            L.y += 1
            if self.hp(L.x, L.y, i):
                break
            i += 1
        if i == 4:
            L.y += 1
            self.trans(L, FALL)
        if L.y > MAXY:
            self.remove(L)
            return False
        return True

    def h1(self, L):                                  # jump, R3
        i = 0
        while i < 2 and self.hp(L.x, L.y - 1, -i - 1):
            i += 1
            L.y -= 1
        if i < 2:
            self.trans(L, WALK)
        self.topcheck(L)
        return True

    def h2(self, L):                                  # dig
        if L.newdig:
            self.dig_row(L, L.y - 2)
            self.dig_row(L, L.y - 1)
            L.newdig = False
        else:
            L.frame = (L.frame + 1) % 16
        if L.frame & 7:
            return False
        y = L.y
        L.y += 1
        if L.y > MAXY:
            self.remove(L)
            return False
        if not self.dig_row(L, y):
            self.trans(L, FALL)
        elif self.oget(L.x, L.y) == DOM_STEEL:
            self.trans(L, WALK)
        return True

    def h3(self, L):                                  # climb
        if L.frame <= 3:
            if not self.hp(L.x, L.y - 7 - L.frame, 0):
                L.y = L.y - L.frame + 2
                self.trans(L, HOIST)
                self.topcheck(L)
            return True
        L.y -= 1
        if L.y - FOOTY[CLIMB] < HEADMINY or \
                self.hp(L.x - L.dx, L.y - 8, -8):
            self.trans(L, FALL, True)
            L.x += L.dx * 2
        return True

    def h4(self, L):                                  # drown
        if L.eoa:
            self.remove(L)
        elif not self.px(L.x + 8 * L.dx, L.y):
            L.x += L.dx
        return False

    def h5(self, L):                                  # hoist
        if L.frame <= 4:
            L.y -= 2
            self.topcheck(L)
            return True
        if L.eoa:
            self.trans(L, WALK)
            self.topcheck(L)
            return True
        return False

    def h6(self, L):                                  # build
        if L.frame == 9 or (L.frame == 10 and L.bricks == 9):
            x = L.x - 4 if L.rtl else L.x
            for i in range(6):
                if 0 <= x + i < W and 0 <= L.y - 1 < H:
                    self.mask[L.y - 1][x + i] = 1
            return False
        if L.frame != 0:
            return True
        L.x += L.dx
        L.y -= 1
        if L.x <= MINX or L.x > MAXX or self.hp(L.x, L.y - 1, -1):    # R5
            self.trans(L, WALK, True)
            self.topcheck(L)
            return True
        L.x += L.dx
        if self.hp(L.x, L.y - 1, -1):
            self.trans(L, WALK, True)
            self.topcheck(L)
            return True
        L.bricks -= 1
        if L.bricks == 0:
            self.trans(L, SHRUG)
            self.topcheck(L)
            return True
        if self.hp(L.x + L.dx * 2, L.y - 9, -9) or L.x <= MINX or \
                L.x > MAXX:
            self.trans(L, WALK, True)
            self.topcheck(L)
            return True
        if L.y - FOOTY[BUILD] < HEADMINY:
            self.trans(L, WALK)
            self.topcheck(L)
        return True

    def stopped(self, L, o):
        return o == DOM_STEEL or (o == DOM_OWL and L.dx != -1) or \
            (o == DOM_OWR and L.dx != 1)

    def h7(self, L):                                  # bash
        f = L.frame % 16
        if f > 10:
            L.x += L.dx
            if not MINX <= L.x <= MAXX:                # R5
                self.trans(L, WALK, True)
                return True
            i = 0
            while i < 3 and not self.hp(L.x, L.y, i):
                i += 1
                L.y += 1
            if i == 3:
                self.trans(L, FALL)
                return True
            if self.stopped(L, self.oget(L.x + L.dx * 8, L.y - 8)):
                self.trans(L, WALK, True)
            return True
        if 2 <= f <= 5:
            self.apply(1 if L.rtl else 0, f - 2, L.x - 8, L.y - 10)
            if L.frame == 5:
                i = 0
                x = L.x + L.dx * 8
                while i < 4 and not self.px(x, L.y - 6):
                    i += 1
                    x += L.dx
                if i == 4:
                    self.trans(L, WALK)
        return False

    def h8(self, L):                                  # mine
        row = 3 if L.rtl else 2
        if L.frame == 1:
            self.apply(row, 0, L.x - 8, L.y - 13)
            return False
        if L.frame == 2:
            self.apply(row, 1, L.x + L.dx - 8, L.y + 1 - 13)
            return False
        if L.frame in (3, 15):
            for _ in range(2):
                L.x += L.dx
                if not MINX <= L.x <= MAXX:
                    self.trans(L, WALK, True)
                    return True
            if L.frame == 3:
                L.y += 1
                if L.y > MAXY:
                    self.remove(L)
                    return False
            if not self.hp(L.x, L.y, 0):
                self.trans(L, FALL)
                return True
            b = self.oget(L.x, L.y)
            if b == DOM_STEEL or (b == DOM_OWL and L.dx != -1) or \
                    b == DOM_OWR:                      # R7, the bug
                self.trans(L, WALK, True)
            return True
        if L.frame == 0:
            L.y += 1
            if L.y > MAXY:
                self.remove(L)
                return False
            return True
        return False

    def h9(self, L):                                  # fall, R4
        if L.fallen > 16 and L.float:
            self.trans(L, FLOAT)
            return True
        i = 0
        while i < 3 and not self.hp(L.x, L.y, i):
            i += 1
            L.y += 1
            if L.y > MAXY:
                self.remove(L)
                return False
        if i == 3:
            if L.fallen < 250:
                L.fallen += 3
            return True
        self.trans(L, SPLAT if L.fallen > 60 else WALK)
        return True

    def h10(self, L):                                 # float
        i = L.fidx
        L.frame = FLOAT_FR[i]
        dy = FLOAT_DY[i]
        L.fidx = 8 if L.fidx + 1 >= 16 else L.fidx + 1
        if dy <= 0:
            L.y += dy
        else:
            miny = 0
            while dy > 0:
                if self.hp(L.x, L.y, miny):
                    self.trans(L, WALK)
                    return True
                L.y += 1
                dy -= 1
                miny += 1
        if L.y > MAXY:
            self.remove(L)
            return False
        return True

    def h11(self, L):                                 # splat
        if L.eoa:
            self.remove(L)
        return False

    def h12(self, L):                                 # exit
        if L.eoa:
            self.remove(L)
            self.savedn += 1
        return False

    h13 = h11                                         # fry

    def h14(self, L):                                 # block
        if not self.hp(L.x, L.y, 0):
            self.trans(L, WALK)
            L.block = False
            self.field(L, False)
        return False

    def h15(self, L):                                 # shrug
        if L.eoa:
            self.trans(L, WALK)
            return True
        return False

    def h16(self, L):                                 # ohno
        if L.eoa:
            self.trans(L, BOMB)
            return False
        i = 0
        while i < 3 and not self.hp(L.x, L.y, i):
            i += 1
            L.y += 1
        if L.y > MAXY:
            self.remove(L)
            return False
        return True

    def h17(self, L):                                 # bomb, R4
        if not L.eoa:
            return False
        if L.block:
            L.block = False
            self.field(L, False)
        if self.oget(L.x, L.y) not in (DOM_STEEL, DOM_WATER):
            self.apply(4, 0, L.x - 8, L.y - 14)
        self.remove(L)
        self.pfin = PARTICLE_FRAMECOUNT               # R9
        return False

    def objects_check(self, L):
        b = self.oget(L.x, L.y)
        L.below = b
        L.front = self.oget(L.x + 8 * L.dx, L.y - 8)
        if b == DOM_NONE:
            return
        if b < 128:
            o = self.info[b]
            if not o["trig"]:
                o["trig"] = True
                o["frame"] = 0
                self.remove(L)
            return
        if b == DOM_EXIT:
            if L.act != FALL:                         # R6
                self.trans(L, EXIT)
        elif b == DOM_FL:
            if L.dx > 0:
                L.rtl = not L.rtl
        elif b == DOM_FR:
            if L.dx < 0:
                L.rtl = not L.rtl
        elif b == DOM_WATER:
            self.trans(L, DROWN)
        elif b == DOM_FIRE:
            self.trans(L, FRY)

    def fuse(self, L):
        L.timer -= 1
        if L.timer > 0:
            return False
        self.trans(L, BOMB if L.act in (FRY, DROWN, FLOAT, FALL) else OHNO)
        return True

    # --- the skills (by the script, at a named lemming) ---------------------
    def hit(self, cx, cy, right):
        """PrioritizedHitTest (Lemmix :3595) at world (cx, cy): the 13x13 box
        off each lemming's frame top left, the last prioritised one winning
        unless the right button is held - and then ProcessSkillAssignment's
        RIGHT-CLICK GLITCH (R8) when that leaves nobody."""
        n, prio, norm = 0, None, None
        for i, L in enumerate(self.lems):
            if L.gone:
                continue
            x, y = L.x - FOOTX[L.act], L.y - FOOTY[L.act]
            if x <= cx <= x + 12 and y <= cy <= y + 12:
                n += 1
                if L.act in PRIO:
                    prio = i
                else:
                    norm = i
        if norm is not None:
            self.hitlast = norm
        l1 = norm if (right or prio is None) else prio
        if n == 0:
            return False
        if l1 is None:
            l1 = self.hitlast                  # R8, the right-click glitch
        if l1 is None:
            return False
        return self.assign(l1, norm)

    def assign(self, i, j=None):
        """AssignClimber .. AssignDigger to lemming i, with j the hit test's
        non-prioritised Lemming2 that the last four fall back to. A REMOVED
        lemming is not refused, because Lemmix's is not: the right-click
        glitch can name one, and the count is spent on it."""
        k = self.skill - 2
        if not 0 <= k <= 7 or self.cnt[k] <= 0 or i >= len(self.lems):
            return False
        L = self.lems[i]
        L2 = self.lems[j] if j is not None else None
        a = L.act
        ok = False
        if self.skill == 2:
            if not L.climb and a not in (BLOCK, SPLAT, BOMB):
                L.climb = True
                if a == SHRUG:
                    L.act = WALK                       # R7, "literally"
                ok = True
        elif self.skill == 3:
            if not L.float and a not in (BLOCK, SPLAT, BOMB):
                L.float = ok = True
        elif self.skill == 4:
            if L.timer == 0 and a not in (OHNO, BOMB, FRY, SPLAT):
                L.timer = 79
                ok = True
        elif self.skill == 5:
            if a in SETS["blk"] and not self.field_over(L):
                self.trans(L, BLOCK)
                ok = True
        elif self.skill == 6:
            S = self.pick(L, L2, SETS["bld"])
            if L.y - FOOTY[a] >= HEADMINY and S:
                self.trans(S, BUILD)
                ok = True
        elif self.skill in (7, 8):
            S = self.pick(L, L2, SETS["bsh" if self.skill == 7 else "min"])
            if S and S.front != DOM_STEEL and \
                    not (self.skill == 8 and S.below == DOM_STEEL) and \
                    not (S.front == DOM_OWL and S.dx != -1) and \
                    not (S.front == DOM_OWR and S.dx != 1):
                self.trans(S, BASH if self.skill == 7 else MINE)
                ok = True
        else:
            S = self.pick(L, L2, SETS["dig"])
            if L.below != DOM_STEEL and S:
                self.trans(S, DIG)
                ok = True
        if ok:
            self.cnt[k] -= 1
        return ok

    @staticmethod
    def pick(L, L2, actions):
        """Lemming1 when its action is in the set, else Lemming2 when ITS
        is (AssignBuilder .. AssignDigger, Lemmix :2103-2216)."""
        if L.act in actions:
            return L
        if L2 is not None and L2.act in actions:
            return L2
        return None

    def field_over(self, L):
        for fy in (-6, -2, 2):
            for fx in (-4, 0, 4):
                if self.oget(L.x + fx, L.y + fy) in (DOM_FL, DOM_BLOCKER,
                                                     DOM_FR):
                    return True
        return False

    # --- the frame (Lemmix InternalUpdate's order) ---------------------------
    def frame(self):
        if self.finished:
            return
        if self.pfin <= 0:                            # R9
            if self.mins <= 0 and self.secs <= 0:
                self.timeup = self.finished = True
                return
            if self.savedn >= self.maxlem or self.removed >= self.maxlem \
                    or (self.nuked and self.out == 0):
                self.finished = True
                return
        if self.rrdir:
            self.rr = max(self.rrmin, min(99, self.rr + self.rrdir))
        if self.paused:
            return
        self.it += 1
        if self.glitchp <= 0:                  # R8, the pause glitch
            self.clockf += 1
        else:
            self.glitchp -= 1
        if self.pfin > 0:                      # R9 (Game.pas:3508)
            self.pfin -= 1
        if self.clockf >= 17:
            self.clockf = 0
            self.secs -= 1
            if self.secs < 0:
                if self.mins > 0:
                    self.mins, self.secs = self.mins - 1, 59
                else:
                    self.mins = self.secs = 0
        if self.it == 35:
            self.opened = True
            for e in self.ents:
                e["trig"], e["frame"] = True, 1
        # spawn
        if self.opened and not self.nuked:
            self.countdown -= 1
            if self.countdown == 0:
                n = 99 - self.rr
                if n < 0:
                    n += 256
                self.countdown = n // 2 + 4
                if self.released < self.maxlem and self.ents:
                    e = self.ents[self.order[self.released & 3]]
                    self.lems.append(Lem(e["x"] + 24, e["y"] + 14))
                    self.released += 1
                    self.out += 1
        # the lemmings
        for L in self.lems:
            if L.gone:
                continue
            if L.timer and self.fuse(L):
                continue
            if self.handle(L):
                self.objects_check(L)
        # the nuke
        if self.nuked and self.nukeing:
            while self.nukeix < self.released and \
                    self.lems[self.nukeix].gone:
                self.nukeix += 1
            if self.nukeix > self.released - 1:
                self.nukeing = False
            else:
                L = self.lems[self.nukeix]
                if L.timer == 0 and L.act not in (SPLAT, BOMB):
                    L.timer = 79
                self.nukeix += 1
        # the objects
        entstep = not self.entdone and self.it >= 35
        for o in self.objs:
            n = self.types[o["t"]]["frames"]
            if o["t"] == 1:
                if not entstep or not o["trig"]:
                    continue
                o["frame"] += 1
                if o["frame"] >= n:
                    o["frame"], o["trig"] = 0, False
                    self.entdone = True
                continue
            if o["trig"] or self.types[o["t"]]["anim"] == 2:
                o["frame"] += 1
            if o["frame"] >= n:
                o["frame"], o["trig"] = 0, False

    def line(self, n):
        out = ["%d %d %d %d %d %d %d %d %d" % (n, self.released, self.out,
                                               self.savedn, self.removed,
                                               self.rr, self.mins, self.secs,
                                               self.maxlem)]
        for i, L in enumerate(self.lems):
            fl = (L.rtl << 0) | (L.gone << 1) | (L.climb << 2) | \
                (L.float << 3) | (L.block << 4)
            out.append("%d:%d,%d,%d,%d,%d,%d" % (i, L.x, L.y, L.act, L.frame,
                                                 fl, L.timer))
        return " ".join(out)


def run(fx, level, script, frames):
    g = Game(fx, level)
    cmds = {}
    for raw in open(script):
        a = raw.split("#")[0].split()
        if a:
            cmds.setdefault(int(a[0]), []).append(a[1:])
    lines = []
    for n in range(1, frames + 1):
        for c in cmds.get(n, []):
            if c[0] == "S":
                g.skill = int(c[1])
            elif c[0] == "A":
                g.assign(int(c[1]))
            elif c[0] == "H":
                g.hit(int(c[1]), int(c[2]), int(c[3]))
            elif c[0] in ("h", "n"):
                # MID-FRAME (the package queues it while a frame is part-
                # stepped): it lands before the NEXT frame, as Lemmix's do
                cmds.setdefault(n + 1, []).append([c[0].upper()] + c[1:])
            elif c[0] == "N":
                if not g.nuked:
                    g.maxlem = g.released          # R8, the nuke glitch
                    g.nuked = g.nukeing = True
                    g.nukeix = 0
            elif c[0] == "R":
                g.rrdir = int(c[1])
            elif c[0] == "P":
                g.paused = not g.paused
                if g.paused and g.it < 34 and g.glitchp == 0:
                    g.glitchp = 34 - g.it          # R8, the pause glitch
        g.frame()
        lines.append(g.line(n))
        if g.finished:
            break
    return lines


FIELDS = ("x", "y", "action", "frame", "flags", "fuse")
HEAD = ("frame", "released", "out", "saved", "removed", "rate", "minutes",
        "seconds", "maxlem")


def divergence(want, got):
    """The first place two traces disagree, in words: the frame, the lemming
    and the field, which is what makes a slip findable in the C."""
    for n, (w, g) in enumerate(zip(want, got), 1):
        if w == g:
            continue
        ws, gs = w.split(), g.split()
        for k in range(min(len(ws), len(gs))):
            if ws[k] == gs[k]:
                continue
            if k < len(HEAD):
                return "frame %d: %s is %s, the package says %s" % (
                    n, HEAD[k], ws[k], gs[k])
            i, wv = ws[k].split(":")
            gv = gs[k].split(":")[1]
            for f, a, b in zip(FIELDS, wv.split(","), gv.split(",")):
                if a != b:
                    return ("frame %d: lemming %s's %s is %s, the package "
                            "says %s (lemsim %s, package %s)" %
                            (n, i, f, a, b, ws[k], gs[k]))
        return "frame %d: %d lemmings against %d" % (n, len(ws) - len(HEAD),
                                                     len(gs) - len(HEAD))
    if len(want) != len(got):
        return "the reference ran %d frames and the package %d" % (
            len(want), len(got))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--level", type=int, required=True)
    ap.add_argument("--script", required=True)
    ap.add_argument("--frames", type=int, default=2000)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--sha256", help="the committed hash of this trace: the "
                    "reference itself has not drifted")
    ap.add_argument("--compare", help="the package's trace (lemtest --mech) "
                    "to diff against this one")
    a = ap.parse_args()
    lines = run(Fixture(a.fixture), a.level, a.script, a.frames)
    text = "\n".join(lines) + "\n"
    with open(a.out, "w") as f:
        f.write(text)
    print("lemsim: level %d, %d frames -> %s" % (a.level, len(lines), a.out))
    if a.sha256:
        import hashlib
        h = hashlib.sha256(text.encode()).hexdigest()
        if h != a.sha256:
            print("lemsim: level %d's trace is %s, the committed one %s - the "
                  "REFERENCE moved (tools/lemsim.py, the fixture or %s). If "
                  "that was meant, recommit the hash." %
                  (a.level, h, a.sha256, a.script))
            return 1
    if a.compare:
        got = open(a.compare).read().splitlines()
        d = divergence(lines, got)
        if d:
            print("lemsim: level %d DIVERGES - %s" % (a.level, d))
            return 1
        print("lemsim: level %d, the package agrees on every frame" % a.level)
    return 0


if __name__ == "__main__":
    sys.exit(main())
