#!/usr/bin/env python3
"""os88logovid.py - the os8088 logo VIDEO (VIDEO-PLAN 14.3): generated, and
then committed as a .V88, because it changes rarely and making it takes
numpy, which the build does not need (VIDEO-PLAN 14.7).

    python3 tools/os88logovid.py --preview DIR [--ground board|dither]
    python3 tools/os88logovid.py -o apps/video/os8088.v88
    python3 tools/os88logovid.py -o OS8088S.V88 --sound   # the listening copy
    python3 tools/os88logovid.py --wav OS8088.WAV         # its sound alone

WHAT IT SHOWS, in the owner's words: a DIP chip with wires running out of
it; lit 1s and 0s travel along the wires, into the chip and out of it; the
chip starts blank and then the os8088 logo is emblazoned on it - in a
"2000s commercial" style rather than OS8088.GIF's flat one (tools/
os88logo.py, SPEC.md 63), which is where the WORDMARK still comes from: the
letters on the package are that tool's pen strokes, so the video and the
still are one brand.

HOW: a small scene in millimetres - a 40-pin DIP 52 x 14 x 4.5 on a board -
seen by a pinhole camera from the front, the right and above (the
three-quarter view). Every plane in it (the board, the package's faces, a
pin's shoulder and leg) is a HOMOGRAPHY from its own 2D coordinates to the
screen, so a pixel is shaded by mapping it BACK onto the frontmost plane
under it. It is rendered at each adapter's own pixel shape - VGA's square
640 x 480, Hercules' 720 x 348 and CGA's 640 x 200, pixels 1.0, 1.55 and
2.40 times as tall as they are wide - to the same PHYSICAL size, so the
chip is the same shape on every screen and no picture is scaled after it
is drawn. Greys become one bit by an 8 x 8 Bayer threshold anchored to the
canvas, so what does not move costs nothing from frame to frame; the
moving parts (the bits, the burn, the sweep) are what the file pays for.

THE TIMELINE, at 15 fps: the bits flow in to a blank chip; each letter
IGNITES as they reach it and burns along its own strokes (a geodesic
distance through the letter's ink from its ignition point); a light sweeps
the package once; and from frame LOOP on the scene is periodic - every bit
in step with a period that divides the loop's length - so the file's seam
(SPEC.md 98.1.1.2) takes the last frame back to frame LOOP exactly, and the
lettering stays once it has appeared.

THE BITS (SPEC.md 98.3.11): each is an EMISSION with a time of its own - it
enters at the canvas edge or leaves a pin's foot, rides the whole trace
with one digit, and is gone at the other end, the pin or past the edge
(clipped there, so it slides rather than pops). The intro is the same rule
with a start: nothing reaches a pin before BIT_IN, the chip sends nothing
before BIT_OUT. build() refuses a file whose frame NFRAMES is not frame
LOOP.
"""
import argparse
import math
import os
import sys
from collections import deque

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88logo                                               # noqa: E402

FPS = 15
NFRAMES = 105                   # 7 s - the long end of the owner's 4 to 7
LOOP = 57                       # the frame the seam goes back to
PERIOD = NFRAMES - LOOP         # 48: every loop motion's period divides it

# the adapters, at the same PHYSICAL size: half the screen's width and 5/12
# of its height, a canvas 1.6 times as wide as it is tall (not "too wide and
# thin" for a three-quarter view, the owner's L4)
ADAPTERS = {                    # every one a LIN80 canvas: a LIVE file's
    "vga": dict(w=320, h=200),  # shadow is the band GFX_BLIT1 takes, and each
    "herc": dict(w=360, h=144), # names the screen it was drawn for (SPEC.md
    "cga": dict(w=320, h=112),  # 98.3.10)
}
# CGA IS STRETCHED: at its true proportions the canvas is 84 rows, and the
# lettering on a package seen at three quarters is then ten of them - a
# smudge. 112 draws the same picture a third taller, which is the "warp"
# the owner allowed between screens (VIDEO-PLAN 14.7, L5)
PHYS_W, PHYS_H = 1.6, 1.0

BAYER8 = np.array([[0, 32, 8, 40, 2, 34, 10, 42],
                   [48, 16, 56, 24, 50, 18, 58, 26],
                   [12, 44, 4, 36, 14, 46, 6, 38],
                   [60, 28, 52, 20, 62, 30, 54, 22],
                   [3, 35, 11, 43, 1, 33, 9, 41],
                   [51, 19, 59, 27, 49, 17, 57, 25],
                   [15, 47, 7, 39, 13, 45, 5, 37],
                   [63, 31, 55, 23, 61, 29, 53, 21]]) / 64.0 + 1 / 128.0


# ---------------------------------------------------------------------------
# the camera
# ---------------------------------------------------------------------------
def look_at(eye, target, up=(0, 0, 1)):
    eye, target, up = (np.array(v, float) for v in (eye, target, up))
    f = target - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, up)
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return np.stack([r, -u, f]), eye          # rows: screen x, screen y, depth


class Camera:
    """World millimetres to canvas units (PHYS_W x PHYS_H, y down). FIT:
    the points given land inside the box given, as large as they go"""

    def __init__(self, eye, target, fit, box):
        self.R, self.eye = look_at(eye, target)
        q = np.array([self.R @ (np.array(p, float) - self.eye) for p in fit])
        u, v = q[:, 0] / q[:, 2], q[:, 1] / q[:, 2]
        x0, y0, x1, y1 = box
        self.f = min((x1 - x0) / (u.max() - u.min()),
                     (y1 - y0) / (v.max() - v.min()))
        self.cx = (x0 + x1) / 2 - self.f * (u.max() + u.min()) / 2
        self.cy = (y0 + y1) / 2 - self.f * (v.max() + v.min()) / 2

    def project(self, p):
        q = self.R @ (np.array(p, float) - self.eye)
        return np.array([self.cx + self.f * q[0] / q[2],
                         self.cy + self.f * q[1] / q[2]]), q[2]

    def plane(self, origin, u, v):
        """The homography from a plane's (a, b) to the canvas: the plane is
        origin + a u + b v in the world"""
        K = np.array([[self.f, 0, self.cx], [0, self.f, self.cy], [0, 0, 1]])
        M = np.stack([self.R @ np.array(u, float),
                      self.R @ np.array(v, float),
                      self.R @ (np.array(origin, float) - self.eye)], axis=1)
        return K @ M


def to_plane(Hm, X, Y):
    """Canvas points -> the plane's (a, b), through the inverse homography"""
    Hi = np.linalg.inv(Hm)
    d = Hi[2, 0] * X + Hi[2, 1] * Y + Hi[2, 2]
    return ((Hi[0, 0] * X + Hi[0, 1] * Y + Hi[0, 2]) / d,
            (Hi[1, 0] * X + Hi[1, 1] * Y + Hi[1, 2]) / d)


def in_poly(pts, X, Y):
    """Inside a convex polygon (canvas points, either winding)"""
    n = len(pts)
    s = None
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        c = (x1 - x0) * (Y - y0) - (y1 - y0) * (X - x0)
        s = (c >= 0) if s is None else s
        if i == 0:
            pos, neg = c >= 0, c <= 0
        else:
            pos &= c >= 0
            neg &= c <= 0
    return pos | neg


# ---------------------------------------------------------------------------
# the scene
# ---------------------------------------------------------------------------
BODY_L, BODY_W, BODY_Z0, BODY_Z1 = 52.0, 14.0, 1.0, 4.6
PINS, PITCH = 20, 2.54
PIN_Y = BODY_W / 2 + 1.3        # where a leg comes down
_FIT = [(x, y, z) for x in (-BODY_L / 2, BODY_L / 2)
        for y in (-PIN_Y, PIN_Y) for z in (0.0, BODY_Z1)]
CAM = Camera(eye=(70.0, -170.0, 88.0), target=(2.0, 0.0, 0.0), fit=_FIT,
             box=(0.16, 0.22, 1.44, 0.82))


def pin_x(i):
    return (i - (PINS - 1) / 2.0) * PITCH


def wordmark_tex():
    """os88logo's wordmark, big: the texture on the package's top face, and
    its six glyphs' column spans"""
    cap, xh, stroke, gap = 176, 140, 28, 40
    bm = os88logo.wordmark(cap, xh, stroke, gap)
    tex = np.array(bm.px, dtype=bool)          # INK (1) = the letter
    dw, ow, sw = cap * 4 // 5, xh * 8 // 7, xh * 6 // 7
    spans, x = [], 0
    for w in (ow, sw, dw, dw, dw, dw):
        spans.append((x, x + w))
        x += w + gap
    return tex, spans


def burn_times(tex, spans, t0, dt, speed):
    """Each texel's reveal time: glyph i IGNITES at t0 + i dt at the bottom
    of its ink nearest the front, and the burn runs along the strokes - a
    breadth-first geodesic through the glyph's ink - at `speed` texels a
    frame"""
    h, w = tex.shape
    t = np.full(tex.shape, np.inf)
    for gi, (x0, x1) in enumerate(spans):
        ys, xs = np.nonzero(tex[:, x0:x1])
        if not len(ys):
            continue
        k = np.argmax(ys * 1000 - np.abs(xs - (x1 - x0) / 2))
        sy, sx = ys[k], xs[k] + x0
        dist = np.full(tex.shape, -1, int)
        dist[sy, sx] = 0
        q = deque([(sy, sx)])
        while q:
            y, x = q.popleft()
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1),
                           (1, 1), (1, -1), (-1, 1), (-1, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and x0 <= nx < x1 and tex[ny, nx] and \
                        dist[ny, nx] < 0:
                    dist[ny, nx] = dist[y, x] + 1
                    q.append((ny, nx))
        m = dist >= 0
        t[m] = t0 + gi * dt + dist[m] / speed
    return t


class Trace:
    """A board trace from a pin's foot outward: a polyline on the board
    (z = 0), and the bits that ride it.

    A BIT IS AN EMISSION: bit j leaves its spawn end - the canvas edge for
    an inward trace, the pin's foot for an outward one - at frame
    T_j = off + j x gap and rides at `speed` mm a frame until it reaches the
    other end, where it is gone. It is the same bit, with the same digit,
    for the whole of its ride: no bit appears or vanishes part way along,
    and none changes from a 1 to a 0 (the owner's report: they FLIPPED on
    the loop, when a bit's digit belonged to a slot that wrapped round
    rather than to the bit)"""

    def __init__(self, pts, inward, off, gap, speed, start=None):
        self.pts = [np.array(p, float) for p in pts]
        self.seg = [np.linalg.norm(b - a) for a, b in zip(self.pts,
                                                          self.pts[1:])]
        self.length = sum(self.seg)
        self.inward, self.off = inward, off
        self.gap, self.speed = gap, speed       # gap: FRAMES, dividing PERIOD
        self.k = PERIOD // gap                  # bits emitted a loop
        self.start = start                      # outward: the first emission

    def at(self, s):
        """The board point s millimetres from the pin's end"""
        for (a, b), n in zip(zip(self.pts, self.pts[1:]), self.seg):
            if s <= n:
                return a + (b - a) * (s / n)
            s -= n
        return self.pts[-1]

    def riding(self, f):
        """The bits on the trace at frame f, as (j, s): s millimetres from
        the pin's end. The age is (f - j gap) - off, integers first, so
        frame f and frame f + PERIOD give bit j and bit j + k exactly the
        same place - the seam (98.1.1.2) has nothing to round"""
        life = self.length / self.speed
        j1 = int(math.floor((f - self.off) / self.gap))
        j0 = int(math.floor((f - self.off - life) / self.gap))
        for j in range(j0, j1 + 1):
            age = (f - j * self.gap) - self.off
            d = age * self.speed
            if d < 0 or d > self.length:
                continue
            if self.inward:
                # THE INTRO: nothing reaches the pins before BIT_IN, so a
                # bit that would have is never on the trace at all
                if (j * self.gap + self.off) + life < BIT_IN:
                    continue
                yield j, self.length - d
            else:
                # ...and the chip sends nothing before ITS first emission
                if j * self.gap + self.off < self.start:
                    continue
                yield j, d


# EXPERIMENTS (--fx, comma-separated), each off by default so the shipped
# file is the one with none:
#   sweeploop  the light sweep repeats every loop rather than once
#   morph      a front-side bit dissolves into its pin and the pin flashes;
#              an outward one flashes out of the pin and condenses
#   trail      the traces sit dim, and a bit lights its own behind it
#   glow       a bit lights the board round it, a pool in perspective
#   scale      a digit's size follows its distance from the eye
#   pop        no morph: a front bit pops into its pin, which flashes
#   trailbold  the traces stay solid, and a bit's trail is a BOLDER line
#              behind it, tapering back to the trace's one pixel
#   flicker    the letter over the pin a bit enters dims for a moment
#   shadow     each digit casts a hard shadow down and to the right
#   nohalo     a digit has no black ground: its pixels alone, over the scene
#   lift       a digit rides just above its wire rather than centred on it
#
# THE SHIPPED FILE is the owner's pick of 2026-11-03 ("A"): the sweep every
# loop, the front bits morphing into their pins with the flash and its glow
# on the package, and the bold trails - and, the same day, the digits
# TRANSPARENT and riding just above their wires, which with the trail
# reads as skating along them. The rest stay here as the fallbacks for
# room: -lift,-nohalo is 5.6 KB smaller, FACE_GLOW = 0 another 5.7.
# --fx adds to it and -name takes one away, so --fx=-morph,pop is the same
# picture with the bits popping
FX_SHIP = ("sweeploop", "morph", "trailbold", "nohalo", "lift")
FX = set(FX_SHIP)
TRACE_LIT, TRACE_DIM = 0.95, 0.30
TRAIL_MM = 6.0                  # a trail's fall to 1/e, behind its bit
TRAIL_CUT = 3 * TRAIL_MM        # ...and where it ends (5% of its light)
BOLD_MM = 4.0                   # a bold trail's taper, to the trace's width
BOLD_W = 0.8                    # ...from this many pixels' extra half-width
GLOW_MM = 2.6                   # a pool's radius to 1/e
SLIDE_MM = 1.5                  # ...the last of which it slides onto the pin
MORPH_MM = 4.5                  # the last of a trace a bit morphs along
FACE_GLOW = 0.75                # the flash's light on the package's face
FLASH = 6                       # frames a pin stays lit round its bit

BIT_IN = 16                     # the first bit reaches a pin; the burn begins
BIT_OUT = 16                    # the chip's first bit out
# a bit's sprite, in canvas units, at its largest on any adapter: a bit is
# off the canvas once its CENTRE is this far past an edge
SPRITE_HALF = 0.035


def canvas_exit(tr):
    """How far along an outward trace (mm from the pin) a bit has left the
    canvas, sprite and all"""
    m = SPRITE_HALF
    s = 0.0
    while s < tr.length:
        p, _ = CAM.project((*tr.at(s), 0.0))
        if not (-m <= p[0] < PHYS_W + m and -m <= p[1] < PHYS_H + m):
            break
        s += 0.1
    # a lit trail follows a bit off the canvas, so the trace is clear of
    # it only once the bit is that much further on
    tail = TRAIL_CUT if "trail" in FX else \
        BOLD_MM if "trailbold" in FX else 0.0
    return min(tr.length, s + tail)


def make_traces():
    """Traces off chosen pins: straight out, a 45 degree bend, and on to
    beyond the canvas. Every loop motion is periodic in PERIOD frames: a
    trace emits a bit every `gap` frames and gap divides PERIOD.

    THE LOOP IS WHOLE FROM FRAME LOOP: every bit the steady state has on
    the canvas there must have been emitted. An inward one was emitted off
    the canvas, so it can have been at any time; an outward one left the
    chip, which sends nothing before BIT_OUT - so an outward trace's bits
    must cross the canvas in LOOP - BIT_OUT frames, and a long one (up the
    board, behind the chip) rides that much faster. Its pixels are fewer a
    millimetre there, which is why it does not look it"""
    rng = np.random.RandomState(88)
    out = []
    for side in (-1, 1):
        for i in range(PINS):
            if (i * 7 + (side > 0) * 3) % 5 not in (0, 3):
                continue
            x = pin_x(i)
            y0 = side * (PIN_Y + 0.4)
            run = 3.0 + (i % 5) * 1.7
            y1 = y0 + side * run
            bend = -1 if x < 0 else 1
            if (i % 4) == 0:
                bend = -bend
            far = 60.0
            pts = [(x, y0), (x, y1), (x + bend * far, y1 + side * far)]
            speed = [0.9, 1.1, 1.3][i % 3]
            k = [1, 2, 1][(i + (side > 0)) % 3]
            gap = PERIOD // k
            inward = ((i + (side > 0)) % 2) == 0
            off = rng.uniform(0, gap)
            tr = Trace(pts, inward, off, gap, speed, BIT_OUT)
            tr.pin = i
            if not inward:
                tr.speed = max(speed, canvas_exit(tr) /
                               float(LOOP - BIT_OUT - 1))
            out.append(tr)
    return out


# 5 x 7 digits for VGA, and each adapter's own cut of them (a CGA row is 2.4
# pixels tall, so its digit is three rows): readable where the screen can,
# an effect where it cannot - the owner's "they don't have to always be
# clearly readable"
DIGITS = {
    "vga": {"0": ["01110", "10001", "10011", "10101", "11001", "10001",
                  "01110"],
            "1": ["00100", "01100", "00100", "00100", "00100", "00100",
                  "01110"]},
    "herc": {"0": ["01110", "10001", "10001", "10001", "01110"],
             "1": ["00100", "01100", "00100", "00100", "01110"]},
    "cga": {"0": ["01110", "10001", "10001", "01110"],
            "1": ["00100", "01100", "00100", "01110"]},
}


# what a morphing bit becomes: a LINE - the pin's leg - at each adapter's
# digit height (a T, shoulder and leg, read as a letter: the owner's L)
PINGLYPH = {n: ["00100"] * len(d["0"]) for n, d in DIGITS.items()}


# ---------------------------------------------------------------------------
# the renderer
# ---------------------------------------------------------------------------
class Renderer:
    def __init__(self, name, ground="board", ss=3):
        a = ADAPTERS[name]
        self.name, self.w, self.h = name, a["w"], a["h"]
        self.ground = ground
        # supersample positions, in canvas units
        sy = ss * (2 if name == "cga" else 1)
        gx = (np.arange(self.w * ss) + 0.5) / (self.w * ss) * PHYS_W
        gy = (np.arange(self.h * sy) + 0.5) / (self.h * sy) * PHYS_H
        self.X, self.Y = np.meshgrid(gx, gy)
        self.ssx, self.ssy = ss, sy
        self.tex, self.spans = wordmark_tex()
        self.burn = burn_times(self.tex, self.spans, t0=16, dt=4.0,
                               speed=9.0)
        self.traces = make_traces()
        self.pin_near = {}
        self._static()

    # --- planes and coverage -------------------------------------------------
    def poly(self, pts3):
        return [CAM.project(p)[0] for p in pts3]

    def _static(self):
        X, Y = self.X, self.Y
        z0, z1 = BODY_Z0, BODY_Z1
        hl, hw = BODY_L / 2, BODY_W / 2
        # the board, everywhere first
        Hb = CAM.plane((0, 0, 0), (1, 0, 0), (0, 1, 0))
        bx, by = to_plane(Hb, X, Y)
        self.bx, self.by = bx, by
        img = self.board(bx, by)
        # the traces the bits ride: white, ONE PIXEL wide on the screen
        # whatever their distance - drawn in canvas units, at this
        # adapter's pixel size along each axis
        px = PHYS_W / self.w
        self.trace_mask = np.zeros(X.shape, bool)
        self.trace_pix = []             # per trace: (flat index, s in mm)
        for tr in self.traces:
            idx, sv, cum = [], [], 0.0
            for a, b, n in zip(tr.pts, tr.pts[1:], tr.seg):
                pa = CAM.project((*a, 0.0))[0]
                pb = CAM.project((*b, 0.0))[0]
                d = self.seg_dist(X, Y, pa, pb)
                m = d < px * 0.62
                self.trace_mask |= m
                fi = np.flatnonzero(m)
                ab = b - a
                t = ((bx.flat[fi] - a[0]) * ab[0] + (by.flat[fi] - a[1]) *
                     ab[1]) / (ab @ ab)
                idx.append(fi)
                sv.append(cum + np.clip(t, 0, 1) * n)
                cum += n
            self.trace_pix.append((np.concatenate(idx), np.concatenate(sv)))
        img = np.where(self.trace_mask,
                       TRACE_DIM if "trail" in FX else TRACE_LIT, img)
        # the BOLD trail's band: every sample within a couple of pixels of
        # a trace, with its s and its distance from the line IN PIXELS of
        # this adapter, so the taper is the same pixel count everywhere
        self.band = []
        if "trailbold" in FX:
            Xp, Yp = X / PHYS_W * self.w, Y / PHYS_H * self.h
            for tr in self.traces:
                idx, sv, dv, cum = [], [], [], 0.0
                for a, b, n in zip(tr.pts, tr.pts[1:], tr.seg):
                    pa = np.array(self.pixel_of((*a, 0.0)))
                    pb = np.array(self.pixel_of((*b, 0.0)))
                    d = self.seg_dist(Xp, Yp, pa, pb)
                    fi = np.flatnonzero(d < 2.2)
                    ab = b - a
                    t = ((bx.flat[fi] - a[0]) * ab[0] + (by.flat[fi] - a[1])
                         * ab[1]) / (ab @ ab)
                    idx.append(fi)
                    sv.append(cum + np.clip(t, 0, 1) * n)
                    dv.append(d.flat[fi])
                    cum += n
                self.band.append((np.concatenate(idx), np.concatenate(sv),
                                  np.concatenate(dv)))
        # the package's shadow, down and to the right of it
        sh = self.poly([(-hl + 1.5, -hw - 0.5, 0), (hl + 3.5, -hw - 0.5, 0),
                        (hl + 3.5, hw + 2.5, 0), (-hl + 1.5, hw + 2.5, 0)])
        img = np.where(in_poly(sh, X, Y), img * 0.25, img)
        # the far row of pins, behind the package
        self.pinmask_far = self.pins(+1)
        img = np.where(self.pinmask_far[0], self.pinmask_far[1], img)
        # the package: front face (y = -hw), right end (x = +hl), top
        front = self.poly([(-hl, -hw, z0), (hl, -hw, z0), (hl, -hw, z1),
                           (-hl, -hw, z1)])
        end = self.poly([(hl, -hw, z0), (hl, hw, z0), (hl, hw, z1),
                         (hl, -hw, z1)])
        top = self.poly([(-hl, -hw, z1), (hl, -hw, z1), (hl, hw, z1),
                         (-hl, hw, z1)])
        self.top_poly = top
        mf, me, mt = in_poly(front, X, Y), in_poly(end, X, Y), \
            in_poly(top, X, Y)
        Hf = CAM.plane((-hl, -hw, z0), (1, 0, 0), (0, 0, 1))
        fa, fb = to_plane(Hf, X, Y)
        self.fa, self.fb, self.mf = fa, fb, mf
        if self.name == "cga":
            # CGA's rows are 2.4 pixels tall, and a dithered face there is a
            # field of stripes: the faces are flat, the front edged in light
            img = np.where(mf, np.where(fb < 0.30, 0.9, 0.0), img)
            img = np.where(me, 0.5, img)
        else:
            img = np.where(mf, 0.10 + 0.14 * np.clip(fb / (z1 - z0), 0, 1)
                           + 0.06 * np.clip(fa / BODY_L, 0, 1), img)
            img = np.where(me, 0.30, img)
        Ht = CAM.plane((-hl, -hw, z1), (1, 0, 0), (0, 1, 0))
        ta, tb = to_plane(Ht, X, Y)
        self.ta, self.tb, self.mt = ta, tb, mt
        # a GLOSSY black: a broad, soft highlight toward the back left
        topshade = 0.12 * np.exp(-(((ta - BODY_L * 0.28) / (BODY_L * 0.30))
                                   ** 2 + ((tb - BODY_W * 0.80) /
                                           (BODY_W * 0.45)) ** 2))
        # the pin-1 notch: a half disc cut into the left end
        notch = (ta ** 2 + (tb - hw) ** 2) < 2.2 ** 2
        topshade = np.where(notch, 0.20, topshade)
        dot = ((ta - 3.2) ** 2 + (tb - 3.0) ** 2) < 0.9 ** 2
        topshade = np.where(dot, 0.16, topshade)
        img = np.where(mt, topshade, img)
        # a bevel's RIM LIGHT round the top: bright along the edges facing
        # the light (front, right), dimmer along the back and the left
        e = 0.45
        rim = mt & ((tb < e) | (ta > BODY_L - e))
        rim2 = mt & ~rim & ((tb > BODY_W - e) | (ta < e))
        img = np.where(rim, 1.0, np.where(rim2, 0.55, img))
        # the near row of pins, in front of it all
        self.pinmask_near = self.pins(-1)
        img = np.where(self.pinmask_near[0], self.pinmask_near[1], img)
        self.base = img
        # WHAT HIDES A FAR BIT: a trace behind the package runs out from the
        # far pins' feet, so the package and both rows of pins are nearer the
        # eye than any point of it. Per canvas pixel, a majority of its
        # samples - the dither's own grid - so a bit goes behind the chip at
        # the edge the chip is drawn with (the owner's report: they rode OVER
        # it)
        occ = mf | me | mt | self.pinmask_far[0] | self.pinmask_near[0]
        self.occ_far = occ.reshape(self.h, self.ssy, self.w,
                                   self.ssx).mean(axis=(1, 3)) > 0.5
        self.occ_ss = occ
        self.band = [(i[~occ.flat[i]], sv[~occ.flat[i]], dv[~occ.flat[i]])
                     for i, sv, dv in self.band]
        # a trail is seen only where nothing nearer covers its trace
        self.trace_pix = [(i[~occ.flat[i]], sv[~occ.flat[i]])
                          for i, sv in self.trace_pix]

    def seg_dist(self, px, py, a, b):
        ab = b - a
        t = ((px - a[0]) * ab[0] + (py - a[1]) * ab[1]) / (ab @ ab)
        t = np.clip(t, 0, 1)
        dx, dy = px - (a[0] + t * ab[0]), py - (a[1] + t * ab[1])
        return np.hypot(dx, dy)

    def pins(self, side):
        """A row of pins: each a shoulder out of the package and a leg down
        to the board, metallic - light, with a darker leg - as (mask,
        shade)"""
        X, Y = self.X, self.Y
        hw = BODY_W / 2
        mask = np.zeros(X.shape, bool)
        shade = np.zeros(X.shape)
        zs = BODY_Z0 + 1.2
        for i in range(PINS):
            x = pin_x(i)
            y0, y1 = side * hw, side * PIN_Y
            sh = self.poly([(x - 0.75, y0, zs), (x + 0.75, y0, zs),
                            (x + 0.75, y1, zs), (x - 0.75, y1, zs)])
            leg = self.poly([(x - 0.3, y1, zs), (x + 0.3, y1, zs),
                             (x + 0.3, y1, 0), (x - 0.3, y1, 0)])
            ms, ml = in_poly(sh, X, Y), in_poly(leg, X, Y)
            shade = np.where(ms, 0.88, np.where(ml & ~mask, 0.62, shade))
            mask |= ms | ml
            if side < 0:
                self.pin_near[i] = ms | ml
        return mask, shade

    def board(self, bx, by):
        """The ground: a circuit board - dark, a faint weave, routing that
        is not ours in grey and vias - or the desktop's own 50% dither"""
        if self.ground == "dither":
            return np.full(bx.shape, 0.5)
        # CGA's rows are 2.4 pixels tall, and a sparse dot there is a dash:
        # its board is the routing alone
        g = np.full(bx.shape, 0.0 if self.name == "cga" else 0.03)
        # decorative routing: two families of thin lines at 0 and 45 deg
        # (CGA keeps the level family only: a 45 degree line two and a
        # half pixels a row is a staircase, and the board a stair-field)
        fams = ((7.0, 0.0, 0.18, 0.24), (11.0, 45.0, 0.18, 0.20))
        for pitch, ang, wid, val in fams[:1 if self.name == "cga" else 2]:
            c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
            u = bx * c + by * s
            v = -bx * s + by * c
            on = (np.abs((u % pitch) - pitch / 2) < wid) & \
                (((np.floor(v / 13.0) + np.floor(u / pitch)) % 3) == 0)
            g = np.where(on, val, g)
        # vias: small rings on a loose lattice
        vx, vy = (bx % 11.0) - 5.5, (by % 7.0) - 3.5
        r = np.hypot(vx, vy)
        g = np.where((r > 0.45) & (r < 0.9) &
                     (((np.floor(bx / 11) * 3 + np.floor(by / 7)) % 4) == 1),
                     0.8, g)
        return g

    # --- a frame ---------------------------------------------------------------
    def frame(self, f):
        img = self.base.copy()
        # THE BURN: the lettering on the top face, texel by texel
        th, tw = self.tex.shape
        hl, hw = BODY_L / 2, BODY_W / 2
        # the wordmark's box on the top face: centred, 40 mm of its length
        ww = 40.0
        wh = ww * th / tw
        u = (self.ta - (BODY_L - ww) / 2) / ww
        v = (self.tb - (BODY_W - wh) / 2) / wh
        inb = self.mt & (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
        tx = np.clip((u * tw).astype(int), 0, tw - 1)
        ty = np.clip(((1 - v) * th).astype(int), 0, th - 1)
        lit = inb & self.tex[ty, tx] & (self.burn[ty, tx] <= f)
        # the burning front glows past the ink a little
        front = inb & self.tex[ty, tx] & (self.burn[ty, tx] > f) & \
            (self.burn[ty, tx] <= f + 1.5)
        lv = np.ones(img.shape)
        if "flicker" in FX:
            # THE CHIP TAKES A BIT: the letter over the pin it entered by
            # dims for a moment, as if busy
            x0w = (BODY_L - ww) / 2
            for tr in self.traces:
                if not tr.inward:
                    continue
                if not any(0 <= dt < 2 for dt in self.events(tr, f)):
                    continue
                dt = min(dt for dt in self.events(tr, f) if 0 <= dt < 2)
                tx_pin = (pin_x(tr.pin) + hl - x0w) / ww * tw
                for x0g, x1g in self.spans:
                    if x0g - 20 <= tx_pin < x1g + 20:
                        m = (tx >= x0g) & (tx < x1g)
                        lv = np.where(m, np.minimum(lv, 0.35 + 0.3 * dt),
                                      lv)
        img = np.where(lit, lv, np.where(front, 0.55, img))
        # THE SWEEP: once, after the lettering, a band of light across the
        # package - top and front - from the left to the right
        s0, s1 = 44, 56
        fr = f
        if "sweeploop" in FX and f >= s0:
            fr = s0 + (f - s0) % PERIOD     # the sweep, every loop
        if s0 <= fr < s1:
            pos = (fr - s0) / float(s1 - s0 - 1) * 1.4 - 0.2
            band = np.exp(-((self.ta / BODY_L + self.tb / BODY_W * 0.35
                             - pos) / 0.06) ** 2)
            glow = self.mt & ~lit
            img = np.where(glow, np.minimum(1.0, img + 0.85 * band), img)
        img = self.lights(img, f)
        # to one bit, anchored to the pixel grid
        return self.dither(img, f)

    def lights(self, img, f):
        """The experiments that LIGHT something, in grey before the dither:
        a trace behind its bit, the board round it, a pin taking one"""
        flat = img.reshape(-1)
        for ti, tr in enumerate(self.traces):
            near = tr.pts[0][1] < 0
            if "trail" in FX:
                pi, ps = self.trace_pix[ti]
                lum = np.zeros(len(pi))
                for j, sb in tr.riding(f):
                    d = (ps - sb) if tr.inward else (sb - ps)
                    lum = np.maximum(lum, np.where((d >= 0) &
                                                   (d < TRAIL_CUT),
                                                   np.exp(-d / TRAIL_MM), 0))
                flat[pi] = np.maximum(flat[pi], TRACE_DIM +
                                      (TRACE_LIT - TRACE_DIM) * lum)
            if "trailbold" in FX:
                pi, ps, pd = self.band[ti]
                lum = np.zeros(len(pi))
                for j, sb in tr.riding(f):
                    d = (ps - sb) if tr.inward else (sb - ps)
                    lum = np.maximum(lum, np.clip(1.0 - d / BOLD_MM, 0, 1)
                                     * (d >= 0))
                on = pd < 0.5 + BOLD_W * lum
                flat[pi[on]] = 1.0
            if "glow" in FX:
                for j, sb in tr.riding(f):
                    c = tr.at(sb)
                    r = GLOW_MM * 2.5
                    box = [CAM.project((c[0] + dx, c[1] + dy, 0.0))[0]
                           for dx in (-r, r) for dy in (-r, r)]
                    xs = [q[0] for q in box]
                    ys = [q[1] for q in box]
                    W, H = self.X.shape[1], self.X.shape[0]
                    x0 = max(0, int(min(xs) / PHYS_W * W))
                    x1 = min(W, int(max(xs) / PHYS_W * W) + 1)
                    y0 = max(0, int(min(ys) / PHYS_H * H))
                    y1 = min(H, int(max(ys) / PHYS_H * H) + 1)
                    if x0 >= x1 or y0 >= y1:
                        continue
                    sl = (slice(y0, y1), slice(x0, x1))
                    g = 0.55 * np.exp(-((self.bx[sl] - c[0]) ** 2 +
                                        (self.by[sl] - c[1]) ** 2) /
                                      GLOW_MM ** 2)
                    g = np.where(self.occ_ss[sl], 0, g)
                    img[sl] = np.maximum(img[sl], img[sl] + g)
            if ("morph" in FX or "pop" in FX) and near:
                pm = self.pin_near[tr.pin]
                lv = 0.0
                for dt in self.events(tr, f):
                    if -1 < dt < FLASH:
                        lv = max(lv, 1.0 - max(dt, 0) / FLASH)
                if lv > 0:
                    img = np.where(pm, np.maximum(img, 0.62 + 0.6 * lv),
                                   img)
                    # ...and the package takes the light in above the pin:
                    # a glow on its front face, brightest at the pin
                    gx = np.abs(self.fa - (pin_x(tr.pin) + BODY_L / 2))
                    gl = lv * FACE_GLOW * np.exp(-(gx / 1.6) ** 2) * \
                        np.clip(1.0 - (self.fb - 1.0) /
                                (BODY_Z1 - BODY_Z0), 0, 1)
                    img = np.where(self.mf, img + gl, img)
                    flat = img.reshape(-1)
        return np.minimum(img, 1.0)

    def ink(self, spr, under):
        """A digit's sprite over what is under it: with its halo, the
        sprite as it is (black round white); without, its white alone"""
        return spr | under if "nohalo" in FX else spr

    def events(self, tr, f):
        """Frames since each bit's arrival at the pin (inward) or until
        its departure (outward), for the bits near frame f"""
        life = tr.length / tr.speed
        out = []
        for j in range(int(math.floor((f - tr.off - life) / tr.gap)) - 1,
                       int(math.floor((f - tr.off) / tr.gap)) + 2):
            t0 = j * tr.gap + tr.off
            if tr.inward:
                if t0 + life < BIT_IN:
                    continue
                out.append(f - (t0 + life))
            else:
                if t0 < tr.start:
                    continue
                out.append(t0 - f)
        return out

    def dither(self, img, f):
        ss, sy = self.ssx, self.ssy
        h, w = self.h, self.w
        v = img.reshape(h, sy, w, ss).mean(axis=(1, 3))
        thr = np.tile(BAYER8, (h // 8 + 1, w // 8 + 1))[:h, :w]
        out = v > thr
        self.bits(out, f)
        return out

    def bits(self, out, f):
        """The 1s and 0s: sprites at the pixel their board point lands on,
        a black halo round a white digit - and on a FAR trace, only where
        nothing nearer (the package, the pins) covers the pixel"""
        glyphs = DIGITS[self.name]
        gh = len(glyphs["0"])
        gw = 5
        for ti, tr in enumerate(self.traces):
            far = tr.pts[0][1] > 0          # behind the package, from here
            for j, s in tr.riding(f):
                ch = "1" if (ti * 7 + (j % tr.k) * 3) % 5 < 2 else "0"
                if "shadow" in FX and s >= 0.6:
                    p, _ = CAM.project((*tr.at(s), 0.0))
                    qx = int(math.floor(p[0] / PHYS_W * self.w)) - gw // 2
                    qy = int(math.floor(p[1] / PHYS_H * self.h)) - gh // 2
                    ox, oy = (2, 1) if self.name == "cga" else (2, 2)
                    for yy, row in enumerate(glyphs[ch]):
                        for xx, c in enumerate(row):
                            X_, Y_ = qx + xx + ox, qy + yy + oy
                            if c == "1" and 0 <= X_ < self.w and \
                                    0 <= Y_ < self.h and not \
                                    (far and self.occ_far[Y_, X_]):
                                out[Y_, X_] = False
                if "morph" in FX and not far and s < MORPH_MM:
                    self.particles(out, tr, s, glyphs[ch] if
                                   "scale" not in FX else
                                   self.glyph(ch, s, tr))
                    continue
                if s < 0.6:
                    continue                # at the pin: into the chip
                if "scale" in FX:
                    g = self.glyph(ch, s, tr)
                    self.stamp(out, tr, s, g, far)
                    continue
                p, _ = CAM.project((*tr.at(s), 0.0))
                px = int(math.floor(p[0] / PHYS_W * self.w))
                py = int(math.floor(p[1] / PHYS_H * self.h))
                # the bit's digit is the BIT's, periodic in the bits a
                # loop emits, so the seam meets the same digit it left
                g = glyphs[ch]
                x0, y0 = px - gw // 2, py - gh // 2
                if "lift" in FX:
                    y0 = py - gh - 1        # riding ON the wire, not in it
                spr = np.zeros((gh + 2, gw + 2), bool)   # the halo...
                for yy, row in enumerate(g):
                    for xx, c in enumerate(row):
                        if c == "1":
                            spr[yy + 1, xx + 1] = True        # ...the digit
                # CLIPPED to the canvas, so a bit slides in at an edge and
                # off at one rather than popping a pixel short of it
                bx0, by0 = x0 - 1, y0 - 1
                cx0, cy0 = max(bx0, 0), max(by0, 0)
                cx1 = min(bx0 + gw + 2, self.w)
                cy1 = min(by0 + gh + 2, self.h)
                if cx0 >= cx1 or cy0 >= cy1:
                    continue
                spr = spr[cy0 - by0:cy1 - by0, cx0 - bx0:cx1 - bx0]
                box = (slice(cy0, cy1), slice(cx0, cx1))
                vis = ~self.occ_far[box] if far else True
                out[box] = np.where(vis, self.ink(spr, out[box]), out[box])


def _morph_methods():
    def pixel(self, s, tr):
        p, _ = CAM.project((*tr.at(s), 0.0))
        return p[0] / PHYS_W * self.w, p[1] / PHYS_H * self.h

    def glyph(self, ch, s, tr):
        """A digit at the size its distance gives it: drawn from strokes,
        a ring and a stem, at any size - the reference size is the
        hand-cut one"""
        base = DIGITS[self.name][ch]
        bh, bw = len(base), 5
        _, z = CAM.project((*tr.at(s), 0.0))
        _, z0 = CAM.project((0.0, 0.0, 0.0))
        k = z0 / z
        w = int(round(bw * k))
        h = int(round(bh * k))
        w, h = max(3, min(9, w)), max(3, min(bh + 4, h))
        uu = (np.arange(w * 4) + 0.5) / (w * 4)
        vv = (np.arange(h * 4) + 0.5) / (h * 4)
        U, V = np.meshgrid(uu, vv)
        th = 1.15 / w, 1.15 / h             # a stroke about a pixel
        if ch == "0":
            e = ((U - 0.5) / 0.5) ** 2 + ((V - 0.5) / 0.5) ** 2
            ei = ((U - 0.5) / (0.5 - th[0])) ** 2 + \
                ((V - 0.5) / (0.5 - th[1])) ** 2
            m = (e <= 1.0) & (ei > 1.0)
        else:
            m = np.abs(U - 0.5) < th[0] / 2 + 0.02
            m |= (V > 1 - th[1]) & (np.abs(U - 0.5) < 0.32)
            m |= (V < 0.38) & (np.abs((0.5 - U) - (V * 0.5)) < th[0] / 2) \
                & (U < 0.5)
        m = m.reshape(h, 4, w, 4).mean(axis=(1, 3)) > 0.3
        return ["".join("1" if c else "0" for c in row) for row in m]

    def stamp(self, out, tr, s, g, far):
        gh, gw = len(g), len(g[0])
        fx_, fy_ = self.pixel(s, tr)
        x0 = int(math.floor(fx_)) - gw // 2
        y0 = int(math.floor(fy_)) - gh // 2
        spr = np.zeros((gh + 2, gw + 2), bool)
        for yy, row in enumerate(g):
            for xx, c in enumerate(row):
                if c == "1":
                    spr[yy + 1, xx + 1] = True
        bx0, by0 = x0 - 1, y0 - 1
        cx0, cy0 = max(bx0, 0), max(by0, 0)
        cx1, cy1 = min(bx0 + gw + 2, self.w), min(by0 + gh + 2, self.h)
        if cx0 >= cx1 or cy0 >= cy1:
            return
        spr = spr[cy0 - by0:cy1 - by0, cx0 - bx0:cx1 - bx0]
        box = (slice(cy0, cy1), slice(cx0, cx1))
        vis = ~self.occ_far[box] if far else True
        out[box] = np.where(vis, self.ink(spr, out[box]), out[box])

    def particles(self, out, tr, s, g):
        """THE MORPH, in two parts. Riding the last MORPH_MM of the trace
        the digit's pixels flow into a PIN - a little T, a shoulder and a
        leg, the shape the pins are drawn as - each pixel to its own place;
        then over the last SLIDE_MM the pin slides up onto the real one and
        is taken in, which is where it flashes. An outward bit is the same
        film run backwards: the pin flashes, a pin leaves it, and it
        condenses into a digit"""
        pin = PINGLYPH[self.name]
        gh, gw = len(g), len(g[0])
        ph, pw = len(pin), len(pin[0])
        cx, cy = self.pixel(s, tr)
        if s > SLIDE_MM:
            t = (MORPH_MM - s) / (MORPH_MM - SLIDE_MM)
            e = t * t * (3 - 2 * t)
            ox, oy = cx, cy
        else:
            e = 1.0
            # where the pin glyph sits when it IS the pin: its leg's foot
            # on the pin's foot, its shoulder on the pin's shoulder
            x = pin_x(tr.pin)
            fx_, fy_ = self.pixel_of((x, -PIN_Y, 0.0))
            u = 1.0 - max(s, 0.0) / SLIDE_MM
            u = u * u * (3 - 2 * u)
            ox = cx + (fx_ - cx) * u
            oy = cy + (fy_ - (ph - 1) / 2.0 - cy) * u
        src = sorted([(xx - gw // 2, yy - gh // 2)
                      for yy, row in enumerate(g)
                      for xx, c in enumerate(row) if c == "1"],
                     key=lambda q: (q[1], q[0]))
        dst = sorted([(xx - pw // 2, yy - (ph - 1) / 2.0 + (gh - ph) / 2.0
                       * 0)
                      for yy, row in enumerate(pin)
                      for xx, c in enumerate(row) if c == "1"],
                     key=lambda q: (q[1], q[0]))
        ns, nd = len(src), len(dst)
        pairs = {(src[i * ns // nd], dst[i]) for i in range(nd)} | \
            {(src[i], dst[i * nd // ns]) for i in range(ns)}
        pts = set()
        for (sx, sy), (dx, dy) in pairs:
            px = ox + sx + (dx - sx) * e
            py = oy + sy + (dy - sy) * e
            pts.add((int(math.floor(px)), int(math.floor(py))))
        halo = s > SLIDE_MM * 0.35 and "nohalo" not in FX
        if halo:
            for px, py in pts:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        if 0 <= px + dx < self.w and 0 <= py + dy < self.h:
                            out[py + dy, px + dx] = False
        for px, py in pts:
            if 0 <= px < self.w and 0 <= py < self.h:
                out[py, px] = True

    def pixel_of(self, p3):
        p, _ = CAM.project(p3)
        return p[0] / PHYS_W * self.w, p[1] / PHYS_H * self.h

    for fn in (pixel, glyph, stamp, particles, pixel_of):
        setattr(Renderer, fn.__name__, fn)


_morph_methods()


def canvas_bytes(bits):
    """A frame of booleans (True = lit) to MONO1 canvas bytes, g.wb a row"""
    return np.packbits(bits.astype(np.uint8), axis=1).tobytes()


def writer(name, frames, sound=False):
    """One LIVE rendition (SPEC.md 98.3.10): its frames, losslessly - the
    file is small by being drawn small, not by being cut - and two
    keyframes, at 0 and at LOOP: the finished logo, which is the poster"""
    import os88vid as vid
    a = ADAPTERS[name]
    g = vid.Geom(vid.LAY_LIN80, a["w"] // 8, a["h"])
    rate, spf = (FPS * SPF, SPF) if sound else (FPS * 100, 100)
    w = vid.Writer(g, rate, spf, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="os8088", keysecs=LOOP / float(FPS), loop=LOOP)
    surf = g.surface()
    for b in frames:
        cv = canvas_bytes(b)
        ch = []
        for y, base in enumerate(g.base):
            row = cv[y * g.wb:(y + 1) * g.wb]
            for x in range(g.wb):
                if surf[base + x] != row[x]:
                    ch.append(base + x)
            surf[base:base + g.wb] = row
        w.frame(vid.spans(ch, surf, g), surf)
    # TWO keyframes: the start and the finished logo. A resident play from
    # the start reads the block's first record, so key 0 was once left out
    # as ~4 KB a screen buying nothing - and it bought the START's place on
    # the scrub bar: with one key the bar had one place, and a thumb dragged
    # back before the loop snapped back to it (the owner's report). The
    # poster is key 1, the finished logo, and a play from it starts at the
    # loop
    w.keys = [k for k in w.keys if k[0] in (0, LOOP)]
    return w


def build(out, ground="board", pack=None, sound=False):
    """The logo file: RESIDENT, LIVE, a rendition per screen, looping from
    LOOP with Repeat on - SPEC.md 98.1.7, 98.3.10. With `sound`, the
    LISTENING copy (VIDEO-PLAN 14.7, L7): the same pictures and a PCM8
    track, and NOT live - a live play is silent (98.3.10) - so it plays in
    the window or on the full screen, each screen still taking its own
    rendition by the target it names"""
    import os88vid as vid
    ws, names = [], ("vga", "herc", "cga")
    for name in names:
        r, fr = render_all(name, ground)
        # THE SEAM, checked here rather than trusted: the frame after the
        # last must BE frame LOOP, pixel for pixel, or the loop flips
        # something as it joins (the owner's report: digits did)
        d = int((r.frame(NFRAMES) != fr[LOOP]).sum())
        if d:
            raise SystemExit("%s: frame %d is not frame %d (%d pixels): "
                             "the loop would jump at the seam"
                             % (name, NFRAMES, LOOP, d))
        ws.append(writer(name, fr, sound))
    tg = [vid.TARGETS[n] for n in names]
    extra = dict(audio_fmt=vid.AUD_PCM8, abytes=SPF, audio=soundtrack(),
                 targets=tg) if sound else dict(live=tg)
    st = vid.write_resident(out, ws, title="os8088",
                            credits="the os8088 logo", repeat=True,
                            pack=vid.PK_LZB if pack is None else pack,
                            posters=[1] * len(ws), **extra)
    vid.verify_v88(out)
    return st


# ---------------------------------------------------------------------------
# the sound (VIDEO-PLAN 14.7, L7): made to be LISTENED to rather than
# shipped - outside the 120 KB, and the owner's to keep or drop. Unsigned
# 8-bit at 15 x 534 = 8,010 Hz. From LOOP on it is PERIODIC in the loop's
# own length, every partial a whole number of cycles in it, so the seam
# (98.1.1.2) goes back to frame LOOP's sound with no click; the intro's
# events - a pluck as each letter ignites, a swell under the sweep - are
# gone by LOOP. Quiet by intent: a drone and soft data ticks, a thing to
# leave running


SPF = 534                       # samples a frame
RATE = FPS * SPF


def soundtrack():
    n = NFRAMES * SPF
    per = PERIOD * SPF          # the loop, in samples
    t = np.arange(n, dtype=np.float64)
    s0 = LOOP * SPF
    tone = lambda cyc, ph=0.0: np.sin(2 * math.pi * cyc * t / per + ph)
    # THE DRONE: 110, 165 and 220 Hz - 352, 528 and 704 cycles a loop - a
    # slow swell once a loop and a faint 440 shimmer twice
    swell = 1.0 + 0.25 * tone(1, -math.pi / 2)
    drone = swell * (20 * tone(352) + 11 * tone(528, 0.7) +
                     6 * tone(704, 1.9)) + 3.5 * (1 + tone(2)) * tone(1408)
    # THE DATA: sixteen ticks a loop, one every three frames - a 1 high, a 0
    # lower, in a pattern that repeats with the loop
    rng = np.random.RandomState(8088)
    ticks = np.zeros(n)
    tl = int(0.018 * RATE)
    tt = np.arange(tl)
    env = np.sin(math.pi * tt / tl) ** 2
    for k in range(16):
        hi = rng.rand() < 0.5
        f = (1320.0 if hi else 990.0)
        burst = 15 * env * np.sin(2 * math.pi * f * tt / RATE)
        for a in range(k * 3 * SPF, n, per):
            ticks[a:a + tl] += burst[:min(tl, n - a)]
    # the intro: the drone rises from a third, the ticks from nothing - and
    # both are exactly the loop's by LOOP
    ramp = np.clip(t / s0, 0, 1)
    out = drone * (0.35 + 0.65 * ramp) + ticks * np.clip(
        (t - 8 * SPF) / (s0 - 8 * SPF), 0, 1)
    # a PLUCK as each letter ignites (t0 = 16, one every 4 frames), rising
    # through a pentatonic; a SWELL under the sweep (frames 44 to 56). Both
    # are cut to nothing by LOOP, so the seam sees none of either
    ev = np.zeros(n)
    for i, hz in enumerate((220.0, 261.6, 329.6, 392.0, 440.0, 523.3)):
        a = int((16 + 4 * i) * SPF)
        d = t[a:] - a
        ev[a:] += 26 * np.exp(-d / (0.35 * RATE)) * \
            np.sin(2 * math.pi * hz * d / RATE) * np.minimum(1, d / 40.0)
    a, b = 44 * SPF, 57 * SPF
    d = t[a:b] - a
    sw = np.sin(math.pi * d / (b - a)) ** 2
    ev[a:b] += 16 * sw * (np.sin(2 * math.pi * 880 * d / RATE) +
                          0.6 * np.sin(2 * math.pi * 1320 * d / RATE))
    fade = np.clip((s0 - t) / (0.25 * RATE), 0, 1)
    out = out + ev * fade
    return bytes(np.clip(np.round(128 + out), 1, 255).astype(np.uint8))


def wav(path, laps=3):
    """The sound as a WAV, the intro and then `laps` loops - to listen to"""
    import wave
    pcm = soundtrack()
    s0 = LOOP * SPF
    body = pcm + pcm[s0:] * (laps - 1)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(1)
        f.setframerate(RATE)
        f.writeframes(body)


def render_all(name, ground="board", frames=NFRAMES):
    r = Renderer(name, ground)
    return r, [r.frame(f) for f in range(frames)]


def preview(outdir, ground):
    from PIL import Image
    os.makedirs(outdir, exist_ok=True)
    for name, a in ADAPTERS.items():
        r, fr = render_all(name, ground)
        # at a 4:3 screen's proportions: every pixel its physical shape
        sx = 2
        sy = {"vga": 2, "herc": 3, "cga": 5}[name]
        ims = [Image.fromarray((b * 255).astype(np.uint8)).resize(
            (a["w"] * sx, a["h"] * sy), Image.NEAREST) for b in fr]
        ims[0].save(os.path.join(outdir, "%s-%s.gif" % (name, ground)),
                    save_all=True, append_images=ims[1:], duration=67,
                    loop=0)
        for f in (0, 30, 50, LOOP, NFRAMES - 1):
            ims[f].save(os.path.join(outdir, "%s-%s-%03d.png"
                                     % (name, ground, f)))
        print("%s: %d frames at %d x %d" % (name, len(fr), a["w"], a["h"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", metavar="DIR")
    ap.add_argument("--ground", choices=("board", "dither"), default="board")
    ap.add_argument("-o", "--out", help="write the logo's .V88")
    ap.add_argument("--sound", action="store_true",
                    help="the LISTENING copy: a PCM8 track, not live")
    ap.add_argument("--wav", metavar="OUT", help="the sound alone, as a WAV")
    ap.add_argument("--fx", default="",
                    metavar="[-]NAME,...",
                    help="experiments, comma-separated: sweeploop, morph, "
                    "trail, glow, scale")
    a = ap.parse_args()
    for x in a.fx.split(","):
        if x.startswith("-"):
            FX.discard(x[1:])
        elif x:
            FX.add(x)
    if a.wav:
        wav(a.wav)
    if a.preview:
        preview(a.preview, a.ground)
    if a.out:
        st = build(a.out, a.ground, sound=a.sound)
        print("%s: %d bytes; blocks (unpacked, packed) %s"
              % (a.out, st["bytes"], st["blocks"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
