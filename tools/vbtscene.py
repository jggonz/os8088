#!/usr/bin/env python3
"""vbtscene - the ORIGINAL test battle for Video Battle (SPEC.md 125).

Renders every clip of a small battle as CGA mode 4 pixel indexes (0..3,
palette byte 50h: black, light cyan, light red, white), and makes its
music and sound effects as 11,025 Hz PCM. Nothing here is captured from
anything: the scene is drawn from circles, polygons and dither patterns,
and the music is a tune written for it, so it can live in the tree and the
engine can be tested and shown with it.

The battle: two heroes, ARI (sword) and BRAM (staff), against the IRON
BEETLE. The clips are the shapes the engine plays:

    intro          320 x 200, once: the title, then the scene comes up
    idle           a loop of IDLE_N frames, held in memory
    ari_attack     Ari runs in, slashes, runs back
    ari_bolt       a bolt out of the sky onto the beetle
    bram_attack    a fireball from Bram's staff
    bram_heal      sparkles over the party
    beetle_attack  the beetle lunges at the party
    victory        the beetle breaks up; the party cheers (no way back)
    defeat         the party falls (no way back)

Every action starts at the idle loop's frame 0 POSE and ends on it, so the
host tool (os88vbt.py) can join it to any anchor frame and back exactly.
"""

import math
import zlib
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 142            # the battle canvas; the band is rows 142-199
FULL_H = 200
FPS = 15
RATE = 11025
IDLE_N = 24                # the loop's period, frames (1.6 s)

BLACK, CYAN, RED, WHITE = 0, 1, 2, 3

# CGA4 palette byte 50h (SPEC.md 98.1.3.3): black, light cyan, light red,
# white - the RGB the host previews them in
PAL_RGB = [(0, 0, 0), (85, 255, 255), (255, 85, 85), (255, 255, 255)]


# --- drawing helpers ----------------------------------------------------------

def checker(h, w, a, b, phase=0):
    """An h x w checkerboard of colours a and b"""
    y, x = np.mgrid[0:h, 0:w]
    return np.where((x + y + phase) & 1, b, a).astype(np.uint8)


def dither_mask(h, w, level):
    """A 4x4 ordered-dither mask: True where `level` (0..16) is on"""
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                      [3, 11, 1, 9], [15, 7, 13, 5]])
    y, x = np.mgrid[0:h, 0:w]
    return bayer[y & 3, x & 3] < level


class Canvas:
    def __init__(self, h=H):
        self.a = np.zeros((h, W), np.uint8)
        self.h = h

    def img(self):
        im = Image.fromarray(self.a, "L")
        return im

    def paste_mask(self, mask, colour):
        self.a[mask] = colour

    def poly(self, pts, colour, fill_pattern=None):
        im = Image.new("1", (W, self.h), 0)
        ImageDraw.Draw(im).polygon([tuple(p) for p in pts], fill=1)
        m = np.array(im, bool)
        if fill_pattern is not None:
            self.a[m] = fill_pattern[m]
        else:
            self.a[m] = colour

    def ellipse(self, box, colour, pattern=None):
        im = Image.new("1", (W, self.h), 0)
        ImageDraw.Draw(im).ellipse([int(round(v)) for v in box], fill=1)
        m = np.array(im, bool)
        if pattern is not None:
            self.a[m] = pattern[m]
        else:
            self.a[m] = colour

    def line(self, pts, colour, width=1):
        im = Image.new("1", (W, self.h), 0)
        ImageDraw.Draw(im).line([tuple(int(round(v)) for v in p)
                                 for p in pts], fill=1, width=width)
        self.a[np.array(im, bool)] = colour

    def rect(self, x0, y0, x1, y1, colour):
        x0, x1 = max(0, int(x0)), min(W, int(x1))
        y0, y1 = max(0, int(y0)), min(self.h, int(y1))
        if x0 < x1 and y0 < y1:
            self.a[y0:y1, x0:x1] = colour

    def text(self, xy, s, colour, scale=1):
        f = ImageFont.load_default()
        im = Image.new("1", (W * 2, 40), 0)
        ImageDraw.Draw(im).text((0, 0), s, fill=1, font=f)
        m = np.array(im, bool)
        ys, xs = np.nonzero(m)
        if not len(xs):
            return
        m = m[:ys.max() + 1, :xs.max() + 1]
        if scale > 1:
            m = m.repeat(scale, 0).repeat(scale, 1)
        x, y = int(xy[0]), int(xy[1])
        h, w = m.shape
        h, w = min(h, self.h - y), min(w, W - x)
        if h > 0 and w > 0:
            self.a[y:y + h, x:x + w][m[:h, :w]] = colour


# --- the set ------------------------------------------------------------------

def _backdrop():
    c = Canvas()
    # the sky: a band of cyan stars on black, thinning toward the horizon
    rng = np.random.RandomState(7)
    for _ in range(70):
        x, y = rng.randint(0, W), rng.randint(0, 52)
        c.a[y, x] = CYAN if rng.rand() < 0.7 else WHITE
    # distant towers: dark cyan dither silhouettes
    hor = 58
    tower = checker(H, W, BLACK, CYAN)
    xs = [0, 18, 30, 52, 70, 96, 120, 146, 170, 198, 224, 250, 276, 300, 320]
    for i in range(len(xs) - 1):
        top = hor - 6 - (i * 37 % 19)
        c.rect(xs[i] + 2, top, xs[i + 1] - 2, hor, CYAN)
        sub = c.a[top:hor, xs[i] + 2:xs[i + 1] - 2]
        sub[:] = tower[top:hor, xs[i] + 2:xs[i + 1] - 2]
        for wy in range(top + 3, hor - 2, 5):           # lit windows
            for wx in range(xs[i] + 5, xs[i + 1] - 4, 6):
                if (wx * 7 + wy * 3) % 5 == 0:
                    c.a[wy, wx] = WHITE
    c.rect(0, hor, W, hor + 1, CYAN)                    # the horizon
    # the floor: perspective lines to a vanishing point, red dither close up
    vx, vy = 160, 40
    for k in range(-12, 13):
        x_far = vx + k * 12
        x_near = vx + k * 60
        c.line([(x_far, hor + 1), (x_near, H + 40)], RED)
    for j, y in enumerate([62, 67, 74, 83, 95, 111, 131]):
        c.rect(0, y, W, y + 1, RED if j < 3 else CYAN)
    return c.a.copy()


BACKDROP = None


def backdrop():
    global BACKDROP
    if BACKDROP is None:
        BACKDROP = _backdrop()
    return BACKDROP


def shadow(c, cx, cy, rx, ry):
    m = np.zeros_like(c.a, bool)
    y, x = np.mgrid[0:c.h, 0:W]
    m = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1
    c.a[m & ((x + y) % 2 == 0)] = BLACK


def beetle(c, x, y, t, hurt=0, gone=0.0, flash=False):
    """The Iron Beetle at (x, y) = the body's centre; t = the idle phase
    (radians); hurt = a recoil shake; gone = 0..1 dissolved"""
    bob = math.sin(t) * 2.0
    y = y + bob
    x = x + hurt
    shadow(c, x, 112, 58, 7)
    body_pat = np.where(dither_mask(c.h, W, 10), RED, WHITE if flash else BLACK)
    shell_pat = np.where(dither_mask(c.h, W, 14), RED, WHITE)
    keep = None
    if gone > 0:
        keep = ~dither_mask(c.h, W, int(gone * 16))
    before = c.a.copy()
    # six legs: three a side, cyan, swinging with the phase
    for i in range(3):
        sw = math.sin(t + i * 2.1) * 3
        hx = x - 30 + i * 30
        c.line([(hx, y + 8), (hx - 14 + sw, y + 22), (hx - 18 + sw, y + 38)],
               CYAN, 3)
        c.line([(hx + 4, y + 10), (hx + 16 - sw, y + 26), (hx + 12 - sw, y + 40)],
               CYAN, 2)
    # the body: an underside ellipse, then the shell's two halves
    c.ellipse((x - 48, y - 18, x + 48, y + 20), RED, body_pat)
    c.ellipse((x - 44, y - 30, x + 40, y + 10), RED, shell_pat)
    c.line([(x - 2, y - 30), (x - 4, y + 10)], BLACK, 2)
    c.line([(x - 30, y - 18), (x + 26, y - 18)], WHITE)
    # the head, to the RIGHT (it faces the party)
    c.ellipse((x + 30, y - 16, x + 66, y + 14), WHITE if flash else RED,
              None if flash else np.where(dither_mask(c.h, W, 6), WHITE, RED))
    for ex in (x + 46, x + 56):
        c.ellipse((ex - 4, y - 8, ex + 4, y), WHITE)
        c.rect(ex - 1, y - 6, ex + 2, y - 2, BLACK)
    # mandibles, opening with the phase
    op = 3 + math.sin(t * 2) * 2
    c.line([(x + 62, y - 4), (x + 78, y - 8 - op), (x + 84, y - 2)], WHITE, 2)
    c.line([(x + 62, y + 6), (x + 78, y + 10 + op), (x + 84, y + 4)], WHITE, 2)
    # antennae
    c.line([(x + 44, y - 14), (x + 52, y - 34 - bob), (x + 64, y - 40 - bob)], CYAN)
    c.line([(x + 52, y - 14), (x + 66, y - 30 - bob), (x + 78, y - 32 - bob)], CYAN)
    if keep is not None:
        changed = c.a != before
        c.a[changed & ~keep] = before[changed & ~keep]


def hero(c, x, y, t, kind, pose="stand", flash=False, fallen=0.0):
    """A hero standing at foot position (x, y), facing LEFT.
    kind 'ari' (sword, cyan coat) or 'bram' (staff, red robe)."""
    br = math.sin(t) * 1.0
    if fallen > 0:
        # lying down: a body along the ground
        coat = CYAN if kind == "ari" else RED
        c.ellipse((x - 18, y - 6, x + 6, y), WHITE if flash else coat)
        c.ellipse((x + 4, y - 8, x + 14, y), WHITE)
        return
    shadow(c, x, y, 12, 3)
    coat = WHITE if flash else (CYAN if kind == "ari" else RED)
    trim = WHITE if kind == "ari" else CYAN
    hy = y - 30 + br
    # legs
    step = 0
    if pose == "run":
        step = math.sin(t * 3) * 5
    c.line([(x - 2, y - 12 + br), (x - 4 + step, y)], WHITE, 2)
    c.line([(x + 3, y - 12 + br), (x + 5 - step, y)], WHITE, 2)
    # body
    c.poly([(x - 6, hy + 8), (x + 7, hy + 8), (x + 9, y - 10 + br),
            (x - 8, y - 10 + br)], coat)
    c.line([(x - 6, hy + 9), (x + 7, hy + 9)], trim)
    # head
    c.ellipse((x - 5, hy - 2, x + 5, hy + 8), WHITE)
    c.rect(x - 4, hy + 2, x - 2, hy + 4, BLACK)          # an eye, facing left
    if kind == "ari":
        c.poly([(x - 6, hy - 1), (x + 6, hy - 4), (x + 9, hy + 3),
                (x + 4, hy + 1)], CYAN)                    # hair
    else:
        c.poly([(x - 7, hy + 1), (x, hy - 9), (x + 7, hy + 1)], RED)  # hat
        c.line([(x - 7, hy + 1), (x + 7, hy + 1)], WHITE)
    # arm and weapon
    if kind == "ari":
        if pose == "slash":
            c.line([(x - 4, hy + 12), (x - 14, hy + 4)], WHITE, 2)
            c.line([(x - 14, hy + 4), (x - 34, hy - 18)], WHITE, 2)
        elif pose == "raise":
            c.line([(x - 2, hy + 12), (x - 6, hy - 6)], WHITE, 2)
            c.line([(x - 6, hy - 6), (x - 8, hy - 30)], WHITE, 2)
        else:
            c.line([(x - 4, hy + 12), (x - 10, hy + 18)], WHITE, 2)
            c.line([(x - 10, hy + 18), (x - 30, hy + 6 + br)], WHITE, 2)
    else:
        top = (x - 10, hy - 10) if pose == "raise" else (x - 8, hy - 2)
        c.line([(x - 6, hy + 14), top], WHITE, 1)
        c.line([top, (x - 4, y)], CYAN, 2)
        c.ellipse((top[0] - 3, top[1] - 3, top[0] + 3, top[1] + 3),
                  WHITE if pose == "raise" else RED)


# the poses' home positions
BEETLE_XY = (92, 82)
ARI_XY = (282, 104)
BRAM_XY = (250, 126)
ARI_PH, BRAM_PH = 0.0, 1.3      # breathing phase offsets


def scene(t, beetle_kw=None, ari=None, bram=None, overlay=None, flash=False):
    """One frame of the battle: t = the idle phase in radians"""
    c = Canvas()
    c.a[:] = backdrop()
    if flash:
        c.a[(c.a == BLACK)] = CYAN
    bk = dict(beetle_kw or {})
    if not bk.get("absent"):
        bk.pop("absent", None)
        beetle(c, bk.pop("x", BEETLE_XY[0]), bk.pop("y", BEETLE_XY[1]), t, **bk)
    # draw the far hero (Ari, higher on the screen) first
    for kind, home, ph, kw in (("ari", ARI_XY, ARI_PH, ari),
                               ("bram", BRAM_XY, BRAM_PH, bram)):
        kw = dict(kw or {})
        hx = kw.pop("x", home[0])
        hy = kw.pop("y", home[1])
        hero(c, hx, hy, t + ph, kind, **kw)
    if overlay:
        overlay(c)
    return c.a


def idle_phase(i):
    return 2 * math.pi * (i % IDLE_N) / IDLE_N


def clip_idle():
    return [scene(idle_phase(i)) for i in range(IDLE_N)]


def ease(u):
    return 0.5 - 0.5 * math.cos(math.pi * max(0.0, min(1.0, u)))


def spark(c, x, y, r, colour=WHITE):
    for k in range(8):
        a = k * math.pi / 4
        c.line([(x + math.cos(a) * r * 0.3, y + math.sin(a) * r * 0.3),
                (x + math.cos(a) * r, y + math.sin(a) * r)], colour)


def clip_ari_attack():
    """Ari runs to the beetle, two slashes, runs home: the hit at frame 14"""
    fr, cues = [], {}
    n_run, n_hit, n_back = 10, 8, 10
    tx = BEETLE_XY[0] + 92
    for i in range(n_run):                     # out
        u = ease((i + 1) / n_run)
        fr.append(scene(0, ari=dict(x=ARI_XY[0] + (tx - ARI_XY[0]) * u,
                                    y=ARI_XY[1] - 10 * math.sin(math.pi * u),
                                    pose="run")))
    for i in range(n_hit):                     # the slash
        def ov(c, i=i):
            if i in (1, 2, 4, 5):
                c.line([(tx - 50, 50 + i * 6), (tx - 10, 100 - i * 4)], WHITE, 2)
                c.line([(tx - 46, 52 + i * 6), (tx - 8, 98 - i * 4)], CYAN, 1)
            if i in (2, 5):
                spark(c, tx - 30, 76, 16)
        fr.append(scene(0, beetle_kw=dict(hurt=(4 if i % 2 else -4) if 1 <= i <= 6
                                          else 0, flash=i in (2, 5)),
                        ari=dict(x=tx, y=ARI_XY[1] - 10,
                                 pose="slash" if i % 3 else "stand"),
                        overlay=ov))
    cues[n_run + 2] = "slash"
    for i in range(n_back):                    # home
        u = ease((i + 1) / n_back)
        fr.append(scene(0, ari=dict(x=tx + (ARI_XY[0] - tx) * u,
                                    y=ARI_XY[1] - 10 * math.sin(math.pi * u),
                                    pose="run" if i < n_back - 1 else "stand")))
    fr.append(scene(0))
    return fr, cues


def bolt_path(x0, y0, x1, y1, seed):
    rng = np.random.RandomState(seed)
    pts = [(x0, y0)]
    n = 7
    for k in range(1, n):
        u = k / n
        pts.append((x0 + (x1 - x0) * u + rng.randint(-10, 11),
                    y0 + (y1 - y0) * u))
    pts.append((x1, y1))
    return pts


def clip_ari_bolt():
    fr, cues = [], {}
    for i in range(6):                         # Ari raises the sword
        def ov(c, i=i):
            if i >= 3:
                spark(c, ARI_XY[0] - 8, ARI_XY[1] - 62, 4 + i, CYAN)
        fr.append(scene(0, ari=dict(pose="raise"), overlay=ov))
    for i in range(10):                        # the bolt
        def ov(c, i=i):
            if i < 7:
                p = bolt_path(BEETLE_XY[0] + 10, 0, BEETLE_XY[0] + 4, 70, i)
                c.line(p, WHITE, 3)
                c.line(p, CYAN, 1)
                spark(c, BEETLE_XY[0] + 4, 70, 10 + i * 2)
        fr.append(scene(0, beetle_kw=dict(hurt=3 if i % 2 else -3,
                                          flash=i % 2 == 0),
                        ari=dict(pose="raise"), overlay=ov))
    cues[6] = "zap"
    for i in range(6):
        fr.append(scene(0, ari=dict(pose="raise" if i < 3 else "stand")))
    fr.append(scene(0))
    return fr, cues


def clip_bram_attack():
    fr, cues = [], {}
    sx, sy = BRAM_XY[0] - 10, BRAM_XY[1] - 40
    tx, ty = BEETLE_XY[0] + 40, BEETLE_XY[1] - 4
    for i in range(5):                         # the staff charges
        def ov(c, i=i):
            c.ellipse((sx - i - 2, sy - i - 2, sx + i + 2, sy + i + 2), RED)
            c.ellipse((sx - 2, sy - 2, sx + 2, sy + 2), WHITE)
        fr.append(scene(0, bram=dict(pose="raise"), overlay=ov))
    n_fly = 8
    for i in range(n_fly):                     # the fireball flies
        u = (i + 1) / n_fly
        fx = sx + (tx - sx) * u
        fy = sy + (ty - sy) * u - 30 * math.sin(math.pi * u)

        def ov(c, fx=fx, fy=fy, i=i):
            for k in range(3):                 # its tail
                c.ellipse((fx + 8 + k * 7 - 3, fy - 3 + k, fx + 8 + k * 7 + 3,
                           fy + 3 - k), RED)
            c.ellipse((fx - 7, fy - 7, fx + 7, fy + 7), RED)
            c.ellipse((fx - 4, fy - 4, fx + 4, fy + 4), WHITE)
        fr.append(scene(0, bram=dict(pose="raise" if i < 2 else "stand"),
                        overlay=ov))
    cues[4] = "shot"
    for i in range(8):                         # the blast
        r = 6 + i * 5

        def ov(c, r=r, i=i):
            if i < 6:
                pat = np.where(dither_mask(c.h, W, 16 - i * 2), RED, WHITE)
                c.ellipse((tx - r, ty - r, tx + r, ty + r), RED, pat)
                c.ellipse((tx - r // 2, ty - r // 2, tx + r // 2, ty + r // 2),
                          WHITE)
        fr.append(scene(0, beetle_kw=dict(hurt=4 if i % 2 else -4,
                                          flash=i < 3), overlay=ov))
    cues[5 + n_fly] = "boom"
    for _ in range(2):
        fr.append(scene(0))
    return fr, cues


def clip_bram_heal():
    fr, cues = [], {}
    rng = np.random.RandomState(3)
    seeds = [(rng.randint(200, 290), rng.randint(70, 130), rng.randint(0, 8))
             for _ in range(14)]
    n = 20
    for i in range(n):
        def ov(c, i=i):
            for (x, y, d) in seeds:
                k = i - d
                if 0 <= k < 12:
                    yy = y - k * 3
                    s = 2 + (k % 3)
                    col = WHITE if k % 2 else CYAN
                    c.line([(x - s, yy), (x + s, yy)], col)
                    c.line([(x, yy - s), (x, yy + s)], col)
        fr.append(scene(0, bram=dict(pose="raise" if i < 14 else "stand"),
                        ari=dict(flash=i in (8, 10)), overlay=ov))
    cues[2] = "chime"
    fr.append(scene(0))
    return fr, cues


def clip_beetle_attack():
    fr, cues = [], {}
    n_in, n_hit, n_out = 9, 8, 10
    reach = 92
    for i in range(n_in):
        u = ease((i + 1) / n_in)
        fr.append(scene(0, beetle_kw=dict(x=BEETLE_XY[0] + reach * u,
                                          y=BEETLE_XY[1] - 8 * math.sin(math.pi * u))))
    for i in range(n_hit):
        def ov(c, i=i):
            if i in (1, 2, 3):
                for k in range(3):
                    c.line([(196 + k * 10, 70 + i * 6), (230 + k * 10, 132 - i * 3)],
                           WHITE, 2)
            if i in (2, 3):
                spark(c, 240, 104, 18, RED)
        kb = 6 if 2 <= i <= 4 else 0
        fr.append(scene(0, beetle_kw=dict(x=BEETLE_XY[0] + reach - (i % 2) * 3),
                        ari=dict(x=ARI_XY[0] + kb, flash=i in (2, 4)),
                        bram=dict(x=BRAM_XY[0] + kb, flash=i in (3, 5)),
                        overlay=ov))
    cues[n_in + 1] = "crash"
    for i in range(n_out):
        u = ease((i + 1) / n_out)
        fr.append(scene(0, beetle_kw=dict(x=BEETLE_XY[0] + reach * (1 - u))))
    fr.append(scene(0))
    return fr, cues


def clip_victory():
    fr, cues = [], {}
    for i in range(24):
        gone = max(0.0, (i - 6) / 16)
        fr.append(scene(0, beetle_kw=dict(flash=i % 2 == 0 and i < 8,
                                          hurt=(3 if i % 2 else -3) if i < 10 else 0,
                                          gone=min(1.0, gone)),
                        ari=dict(y=ARI_XY[1] - (abs(math.sin(i * 0.5)) * 8 if i > 14 else 0),
                                 pose="raise" if i > 14 else "stand"),
                        bram=dict(y=BRAM_XY[1] - (abs(math.sin(i * 0.5 + 1)) * 8 if i > 16 else 0),
                                  pose="raise" if i > 16 else "stand")))
    cues[0] = "boom"
    for i in range(8):
        fr.append(scene(0, beetle_kw=dict(absent=True),
                        ari=dict(pose="raise"), bram=dict(pose="raise")))
    return fr, cues


def clip_defeat():
    fr, cues = [], {}
    for i in range(18):
        u = min(1.0, i / 10)
        fr.append(scene(0, ari=dict(fallen=1.0 if i > 8 else 0.0,
                                    flash=i in (1, 3)),
                        bram=dict(fallen=1.0 if i > 11 else 0.0,
                                  flash=i in (2, 4)),
                        beetle_kw=dict(x=BEETLE_XY[0] + 20 * math.sin(math.pi * u))))
    cues[1] = "crash"
    return fr, cues


def clip_title():
    """OPENING 1, 320 x 200 and played whole with its own sound: the title
    over drifting stars, then the screen goes black"""
    fr = []
    for i in range(40):
        c = Canvas(FULL_H)
        rng = np.random.RandomState(11)
        for _ in range(60):
            x, y = rng.randint(0, W), rng.randint(0, FULL_H)
            c.a[(y + i * 2) % FULL_H, x] = CYAN
        t = min(1.0, i / 10)
        y = int(30 + 40 * (1 - ease(t)))
        if i < 36:
            c.text((54, y), "VIDEO BATTLE", WHITE, scale=3)
        if 12 < i < 36:
            c.text((92, y + 44), "an os8088 demo", CYAN, scale=1)
        fr.append(c.a)
    return fr, {}


def clip_reveal():
    """THE SCENE OPENING: the battle wipes in from the top while the menu is
    already up; its last frame is idle frame 0, and the music comes in"""
    fr = []
    base = scene(0)
    for i in range(14):
        c = Canvas()
        rows = int(H * min(1.0, (i + 1) / 12))
        c.a[:rows] = base[:rows]
        fr.append(c.a)
    for i in range(4):
        fr.append(base.copy())
    return fr, {}


def ring_overlay(cx, cy, r, colour, width=3):
    """A ring of light: an ellipse's OUTLINE, `width` pixels thick"""
    def ov(c):
        im = Image.new("1", (W, c.h), 0)
        ImageDraw.Draw(im).ellipse([int(cx - r), int(cy - r * 0.6),
                                    int(cx + r), int(cy + r * 0.6)],
                                   outline=1, width=width)
        c.a[np.array(im, bool)] = colour
    return ov


def clip_ari_limit():
    """ARI'S LIMIT: nine seconds of a lot moving - a leap, twelve slashes,
    rings of light over the whole field, the beetle thrown about. It is
    there to be far bigger than memory, so the engine has to STREAM it"""
    fr, cues = [], {}
    tx, ty = BEETLE_XY[0] + 70, BEETLE_XY[1] - 6
    n_up, n_cuts, n_rings, n_down = 14, 48, 40, 14
    for i in range(n_up):                       # the leap
        u = ease((i + 1) / n_up)
        fr.append(scene(i * 0.4, ari=dict(x=ARI_XY[0] + (tx + 30 - ARI_XY[0]) * u,
                                          y=ARI_XY[1] - 60 * math.sin(math.pi * u * 0.9),
                                          pose="raise")))
    cues[2] = "limit_rise"
    rng = np.random.RandomState(4)
    for i in range(n_cuts):                     # twelve slashes, four frames each
        k = i // 4
        a0 = rng.uniform(0, math.pi)

        def ov(c, i=i, k=k, a0=a0):
            if i % 4 in (0, 1, 2):
                r = 70
                c.line([(tx + math.cos(a0) * r, ty + math.sin(a0) * r * 0.6),
                        (tx - math.cos(a0) * r, ty - math.sin(a0) * r * 0.6)],
                       WHITE, 3)
                c.line([(tx + math.cos(a0) * r, ty + math.sin(a0) * r * 0.6 + 2),
                        (tx - math.cos(a0) * r, ty - math.sin(a0) * r * 0.6 + 2)],
                       CYAN, 1)
            if i % 4 == 1:
                spark(c, tx, ty, 22 + (k % 3) * 6)
        sx = tx + 40 + 20 * math.sin(i * 0.7)
        sy = ARI_XY[1] - 30 + 16 * math.cos(i * 0.9)
        fr.append(scene(i * 0.5, beetle_kw=dict(hurt=(6 if i % 2 else -6),
                                                flash=i % 4 == 1),
                        ari=dict(x=sx, y=sy, pose="slash" if i % 2 else "raise"),
                        overlay=ov))
        if i % 8 == 1:
            cues[n_up + i] = "slash"
    for i in range(n_rings):                    # the rings
        def ov(c, i=i):
            for j in range(2):
                r = ((i // 2) * 10 + j * 70) % 150   # (on every other frame)
                if r > 4:
                    ring_overlay(tx, ty, r, (WHITE, CYAN)[j], 2)(c)
            if i < 24:
                spark(c, tx, ty, 10 + (i % 6) * 5, WHITE)
        fr.append(scene(i * 0.6, beetle_kw=dict(hurt=(4 if i % 8 < 4 else -4),
                                                flash=i % 8 == 0, y=BEETLE_XY[1] - 10 * math.sin((i // 4) * 1.2)),
                        ari=dict(x=tx + 40, y=ARI_XY[1] - 40, pose="raise"),
                        overlay=ov))
    cues[n_up + n_cuts] = "limit_burst"
    for i in range(n_down):                     # home
        u = ease((i + 1) / n_down)
        fr.append(scene(0, ari=dict(x=tx + 40 + (ARI_XY[0] - tx - 40) * u,
                                    y=ARI_XY[1] - 40 * (1 - u),
                                    pose="run" if i < n_down - 1 else "stand")))
    fr.append(scene(0))
    return fr, cues


CLIPS = [
    # name, maker, joins back to the idle loop
    ("ari_attack", clip_ari_attack, True),
    ("ari_bolt", clip_ari_bolt, True),
    ("ari_limit", clip_ari_limit, True),
    ("bram_attack", clip_bram_attack, True),
    ("bram_heal", clip_bram_heal, True),
    ("beetle_attack", clip_beetle_attack, True),
    ("victory", clip_victory, False),
    ("defeat", clip_defeat, False),
]


# --- sound ----------------------------------------------------------------------

def _env(n, a, d):
    e = np.ones(n)
    a, d = int(a * RATE), int(d * RATE)
    if a:
        e[:a] = np.linspace(0, 1, a)
    if d:
        e[-d:] *= np.linspace(1, 0, d)
    return e


def music(bars=16):
    """A loop in A minor at 150 BPM, `bars` bars of 1.6 s: a square bass, a
    pulse lead and a noise hat. Sixteen bars are four sections - A, A with
    the lead an octave up, B over a new bass, A again - so a play hears a
    piece of music, not a phrase, and the engine streams it (125.4)"""
    beat = RATE * 60 / 150
    n = int(round(bars * 4 * beat))
    out = np.zeros(n)
    note = lambda m: 440.0 * 2 ** ((m - 69) / 12)
    bass_a = [45, 48, 43, 40]                       # A C G E
    bass_b = [41, 43, 45, 44]                       # F G A G#
    lead_a = [69, 72, 76, 74, 72, 71, 69, 71, 72, 76, 79, 76, 74, 72, 71, 68]
    lead_b = [77, 76, 74, 72, 74, 72, 71, 69, 72, 71, 69, 68, 69, 71, 72, 76]

    def section(bar):
        return (bar // 4) % 4                       # 0 A, 1 A', 2 B, 3 A

    def add(s, x):
        x = x[:max(0, n - s)]
        out[s:s + len(x)] += x

    for bar in range(bars):
        sec = section(bar)
        bl = bass_b if sec == 2 else bass_a
        for b in range(4):
            for half in range(2):
                s = int((bar * 4 + b + half * 0.5) * beat)
                ln = int(beat * 0.5)
                f = note(bl[bar % 4] - (12 if half else 0))
                seg = np.arange(ln) / RATE
                add(s, 0.30 * np.sign(np.sin(2 * np.pi * f * seg)) *
                    _env(ln, 0.002, 0.05))
    for k in range(bars * 4):                       # the lead: a note a beat
        sec = section(k // 4)
        ld = lead_b if sec == 2 else lead_a
        m = ld[k % 16] + (12 if sec == 1 else 0)
        s = int(k * beat)
        ln = int(beat * 0.9)
        seg = np.arange(ln) / RATE
        f = note(m)
        vib = 1 + 0.004 * np.sin(2 * np.pi * 5 * seg)
        w = np.where((seg * f * vib) % 1.0 < 0.25, 1.0, -1.0)  # 25% pulse
        add(s, 0.20 * w * _env(ln, 0.005, 0.12))
    rng = np.random.RandomState(5)
    for k in range(bars * 8):                       # hats on the eighths
        s = int(k * beat / 2)
        ln = int(RATE * (0.06 if k % 4 == 2 else 0.03))
        add(s, 0.10 * rng.uniform(-1, 1, ln) * np.linspace(1, 0, ln))
    return out


def sfx(name):
    # (a seed from the name that is the same in every process: str's
    # hash() is randomised per process, so the bundle was not reproducible)
    rng = np.random.RandomState(zlib.crc32(name.encode()) % 1000)
    if name == "slash":
        n = int(RATE * 0.25)
        x = rng.uniform(-1, 1, n)
        x = np.convolve(x, np.ones(3) / 3, "same")
        return x * np.linspace(1, 0, n) ** 2
    if name == "zap":
        n = int(RATE * 0.6)
        t = np.arange(n) / RATE
        f = 1800 * np.exp(-t * 4) + 200
        ph = np.cumsum(2 * np.pi * f / RATE)
        x = np.sign(np.sin(ph)) * 0.6 + rng.uniform(-0.6, 0.6, n)
        return x * np.linspace(1, 0, n)
    if name == "shot":
        n = int(RATE * 0.3)
        t = np.arange(n) / RATE
        ph = np.cumsum(2 * np.pi * (300 + 1200 * t / t[-1]) / RATE)
        return np.sign(np.sin(ph)) * np.linspace(1, 0, n) * 0.8
    if name == "boom":
        n = int(RATE * 0.7)
        x = rng.uniform(-1, 1, n)
        y = np.zeros(n)
        acc = 0.0
        for i in range(n):                     # a low-passed noise burst
            acc += (x[i] - acc) * 0.08
            y[i] = acc
        y /= max(1e-9, np.abs(y).max())
        return y * np.exp(-np.arange(n) / (RATE * 0.18))
    if name == "chime":
        n = int(RATE * 0.8)
        t = np.arange(n) / RATE
        x = sum(np.sin(2 * np.pi * f * t) * np.exp(-t * d)
                for f, d in ((1320, 4), (1760, 5), (2640, 7)))
        return x / 2.2
    if name == "crash":
        n = int(RATE * 0.45)
        x = rng.uniform(-1, 1, n)
        t = np.arange(n) / RATE
        x = x * 0.7 + 0.5 * np.sign(np.sin(2 * np.pi * 90 * t))
        return x * np.exp(-t * 7)
    if name == "limit_rise":               # a rising sweep under the leap
        n = int(RATE * 1.0)
        t = np.arange(n) / RATE
        ph = np.cumsum(2 * np.pi * (200 + 900 * t) / RATE)
        return np.sign(np.sin(ph)) * np.linspace(0.2, 0.8, n) * np.linspace(1, 0.3, n)
    if name == "limit_burst":              # the rings: a long noisy roar
        n = int(RATE * 2.2)
        x = rng.uniform(-1, 1, n)
        y = np.zeros(n); acc = 0.0
        for i in range(n):
            acc += (x[i] - acc) * 0.15
            y[i] = acc
        y /= max(1e-9, np.abs(y).max())
        t = np.arange(n) / RATE
        return y * np.exp(-t * 1.2) * (0.7 + 0.3 * np.sin(2 * np.pi * 7 * t))
    if name == "fanfare":                  # opening 1's own sound
        notes = [(57, 0.18), (61, 0.18), (64, 0.18), (69, 0.6), (64, 0.18),
                 (69, 1.0)]
        out = []
        for m, d in notes:
            n = int(RATE * d)
            t = np.arange(n) / RATE
            f = 440.0 * 2 ** ((m - 69) / 12)
            w = 0.6 * np.sign(np.sin(2 * np.pi * f * t)) + \
                0.3 * np.sign(np.sin(2 * np.pi * f * 1.5 * t))
            out.append(w * _env(n, 0.005, 0.06))
        out.append(np.zeros(int(RATE * 0.6)))
        return np.concatenate(out) * 0.8
    if name == "riser":                    # the scene opening's own sound
        n = int(RATE * 1.2)
        t = np.arange(n) / RATE
        roll = np.where((t * 16) % 1.0 < 0.3, 1.0, 0.0) * rng.uniform(-1, 1, n)
        ph = np.cumsum(2 * np.pi * (110 + 110 * t / t[-1]) / RATE)
        return (0.5 * roll + 0.4 * np.sign(np.sin(ph))) * np.linspace(0.3, 1, n)
    if name == "tick":                      # the menu's cursor: a blip
        n = int(RATE * 0.035)
        t = np.arange(n) / RATE
        return np.sign(np.sin(2 * np.pi * 1760 * t)) * np.linspace(1, 0, n) * 0.6
    if name == "ok":                        # a command chosen: up a fifth
        n = int(RATE * 0.05)
        t = np.arange(n) / RATE
        a = np.sign(np.sin(2 * np.pi * 988 * t))
        b2 = np.sign(np.sin(2 * np.pi * 1480 * t))
        return np.concatenate([a, b2]) * np.concatenate(
            [np.ones(n), np.linspace(1, 0, n)]) * 0.6
    if name == "deny":                      # not enough MP: a low buzz
        n = int(RATE * 0.18)
        t = np.arange(n) / RATE
        return np.sign(np.sin(2 * np.pi * 110 * t)) * np.linspace(1, 0.2, n) * 0.7
    raise KeyError(name)


SFX = ["slash", "zap", "shot", "boom", "chime", "crash", "limit_rise",
       "limit_burst", "fanfare", "riser"]
UI_SFX = ["tick", "ok", "deny"]


def to_rgb(a):
    pal = np.array(PAL_RGB, np.uint8)
    return pal[a]
