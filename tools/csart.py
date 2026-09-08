#!/usr/bin/env python3
"""CLEAR SKIES' title-screen art (SPEC.md 88.10): the 1bpp bands, generated.

    python3 tools/csart.py -o apps/skies/csart.inc [--preview DIR]

The launcher is a white page with the title lettered across the top and the
AEROPLANE IN USE in front of a cumulus on the right - a band per aircraft,
which a plane record names in CSP_ART (SPEC.md 88.10.1) - and all of them
are DRAWN HERE, as vectors: the lettering as brush strokes (a black stroke
and a narrower white one over it, which is what makes a letter an outline
with a white centre), an aeroplane as filled shapes that hide the cloud
behind them and the lines that make it an aeroplane. Each is rasterised
ONCE into the framebuffer's own 1bpp bit order, so the window puts it up
with a single OSAPI_GFX_BLIT1 (apps/os88api.inc) instead of the four hundred
line calls the drawing would be on an 8088, at SPEC.md 5.6's price each. A
set bit is a LIT pixel there, so the bands carry paper as 1 and ink as 0:
right on a 1bpp adapter as they are, and right on VGA under the blit's
default pen.

EACH AIRCRAFT IS SEEN FROM ITS OWN ANGLE, because a row of side elevations
reads as one drawing with the parts moved around. A side elevation is drawn
flat, in canvas pixels; anything else is written as a MODEL in body
coordinates and projected, the angle being three numbers - see `View` and
`model` below.

The output is checked in and `tests/unit/t_csart.py` regenerates it and
compares, so the include cannot drift from this file. --preview writes every
band as a PNG, zoomed, for looking at.
"""
import argparse
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88lz                      # the packer both the kernel and
                                   # os88pkg.py read (SPEC.md 20.13)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import shot                                     # noqa: E402  (the PNG writer)


class Canvas:
    """One byte a pixel while drawing: 1 = ink, 0 = paper."""

    def __init__(self, w, h):
        assert w % 8 == 0, "a band's width is a multiple of 8 (OSAPI_GFX_BLIT1)"
        self.w, self.h = w, h
        self.px = [bytearray(w) for _ in range(h)]

    def plot(self, x, y, ink=1):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = ink

    def disc(self, cx, cy, r, ink=1):
        r2 = r * r
        for y in range(int(cy - r) - 1, int(cy + r) + 2):
            for x in range(int(cx - r) - 1, int(cx + r) + 2):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                    self.plot(x, y, ink)

    def line(self, x0, y0, x1, y1, ink=1):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.plot(x0, y0, ink)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def polyline(self, pts, ink=1):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            self.line(x0, y0, x1, y1, ink)

    def stroke(self, pts, r, ink=1):
        """A round brush of radius r along the polyline, a step a pixel."""
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            n = max(1, int(math.hypot(x1 - x0, y1 - y0) * 2))
            for i in range(n + 1):
                t = i / n
                self.disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, ink)

    def fill(self, pts, ink=1):
        """Scanline fill of a simple polygon (even-odd)."""
        ys = [p[1] for p in pts]
        for y in range(int(math.floor(min(ys))), int(math.ceil(max(ys))) + 1):
            xs = []
            n = len(pts)
            for i in range(n):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(int(round(a)), int(round(b)) + 1):
                    self.plot(x, y, ink)

    def fillz(self, pts, zb, ink=0):
        """Scanline fill carrying a DEPTH into a z-buffer, so a pixel is taken
        by the nearest face over it and by no other. `pts` are (x, y, depth),
        depth growing toward the camera."""
        ys = [p[1] for p in pts]
        for y in range(int(math.floor(min(ys))), int(math.ceil(max(ys))) + 1):
            xs = []
            n = len(pts)
            for i in range(n):
                (x0, y0, z0), (x1, y1, z1) = pts[i], pts[(i + 1) % n]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    t = (y - y0) / (y1 - y0)
                    xs.append((x0 + t * (x1 - x0), z0 + t * (z1 - z0)))
            xs.sort()
            for (a, za), (b, zbb) in zip(xs[0::2], xs[1::2]):
                ia, ib = int(round(a)), int(round(b))
                for x in range(ia, ib + 1):
                    t = 0.0 if ib == ia else (x - ia) / (ib - ia)
                    z = za + t * (zbb - za)
                    if 0 <= x < self.w and 0 <= y < self.h and z > zb[y][x]:
                        zb[y][x] = z
                        self.px[y][x] = ink

    def linez(self, p0, p1, zb, bias=0.03):
        """A line drawn only where it is not BEHIND what the fills left - the
        hidden-line half of the picture. The bias is the tolerance that lets
        a face's own outline sit on its own fill."""
        (x0, y0, z0), (x1, y1, z1) = p0, p1
        n = max(1, int(round(max(abs(x1 - x0), abs(y1 - y0)))))
        for i in range(n + 1):
            t = i / n
            x, y = int(round(x0 + t * (x1 - x0))), int(round(y0 + t * (y1 - y0)))
            if 0 <= x < self.w and 0 <= y < self.h and z0 + t * (z1 - z0) + bias >= zb[y][x]:
                self.px[y][x] = 1

    def outline_of_mask(self, mask):
        """The boundary of a filled mask (another Canvas): a set pixel with an
        unset 4-neighbour, or on the edge."""
        for y in range(self.h):
            for x in range(self.w):
                if not mask.px[y][x]:
                    continue
                edge = False
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if nx < 0 or ny < 0 or nx >= self.w or ny >= self.h or not mask.px[ny][nx]:
                        edge = True
                        break
                if edge:
                    self.plot(x, y, 1)

    def band(self):
        """The 1bpp rows, bit 7 leftmost, in OSAPI_GFX_BLIT1's order: a SET
        bit is a LIT pixel. Ink is therefore CLEAR here and paper SET - on a
        1bpp adapter lit is white and the band goes up untranslated, and on
        VGA the blit's DEFAULT pen is white for set and black for clear, so no
        package sets a pen either way (apps/os88api.inc, OSAPI_GFX_BLIT1_PEN)."""
        out = []
        for row in self.px:
            b = bytearray(self.w // 8)
            for x, v in enumerate(row):
                if not v:
                    b[x >> 3] |= 0x80 >> (x & 7)
            out.append(bytes(b))
        return out

    def png(self, path, zoom=4):
        w, h = self.w * zoom, self.h * zoom
        pix = bytearray(w * h * 3)
        for y in range(h):
            row = self.px[y // zoom]
            for x in range(w):
                v = 0 if row[x // zoom] else 255
                i = (y * w + x) * 3
                pix[i] = pix[i + 1] = pix[i + 2] = v
        shot.png(path, w, h, bytes(pix))


def arc(cx, cy, rx, ry, a0, a1, n=None):
    """Points on an ellipse from angle a0 to a1 (degrees, y down: 0 = right,
    90 = down, 180 = left, 270 = up), either way round."""
    if n is None:
        n = max(6, int(abs(a1 - a0) / 6))
    pts = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return pts


def bezier(p0, p1, p2, p3, n=24):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        x = u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0]
        y = u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]
        pts.append((x, y))
    return pts


# =============================================================================
# An aeroplane in front of its cloud: 152 x 96, ONE BAND PER AIRCRAFT
# =============================================================================
# Every aircraft is drawn in the same frame and in front of the same cumulus,
# so the launcher's picture changes in the AEROPLANE and in nothing else when
# the Plane list is picked - but each is seen FROM ITS OWN ANGLE (SPEC.md
# 88.10.1), because a row of side elevations reads as one drawing with the
# parts moved around rather than as two aeroplanes.
#
# The cloud is drawn into every band rather than stored once and shared,
# because OSAPI_GFX_BLIT1 is OPAQUE: a plane-only band would punch its whole
# bounding box out of what is under it, and an aeroplane's bounding box is
# most of the cloud. Two bands cost 1,824 bytes each and no code; a
# transparent blit costs a mask the same size as the band it masks, and a
# kernel slot that does not exist.
ART_PLANE_W, ART_PLANE_H = 152, 96


def cumulus(c):
    """The cloud they all stand in front of: a union of discs over a flat
    base, outlined."""
    mask = Canvas(ART_PLANE_W, ART_PLANE_H)
    for cx, cy, r in ((34, 66, 19), (58, 50, 24), (90, 44, 29), (122, 56, 24), (138, 70, 15)):
        mask.disc(cx, cy, r)
    for y in range(83, ART_PLANE_H):                      # the flat bottom
        mask.px[y] = bytearray(ART_PLANE_W)
    c.outline_of_mask(mask)


def solids(c, polys):
    """A FLAT drawing's parts: filled white so they stand in front of the
    cloud, and outlined after - both passes over the whole set, or one
    part's fill erases the part before it's outline. It is enough here
    only because nothing in a side elevation is behind anything; `model`
    below is the same job in three dimensions, where that is false."""
    for poly in polys:
        c.fill(poly, 0)
    for poly in polys:
        c.polyline(poly + [poly[0]], 1)


def plane_art(draw):
    c = Canvas(ART_PLANE_W, ART_PLANE_H)
    cumulus(c)
    draw(c)
    return c


# --- a VIEW of an aeroplane: the model in three dimensions, projected --------
# Anything but a side elevation needs foreshortening, which is wrong whenever
# it is guessed - so an aircraft that is not drawn side on is written once as
# a model in its own body coordinates and the angle it is seen from is THREE
# NUMBERS. That is what makes a new angle a one-line change rather than a
# redrawing, and it is the simulator's own arithmetic (SPEC.md 88.5) in
# Python and to no budget: the machine only ever sees the raster.
#
# Body coordinates are an aeroplane's own: x out the RIGHT wing, y UP,
# z FORWARD out of the nose. The camera is on +z looking back along it, so
# a larger z is nearer.
def rot(pts, yaw, pitch, roll):
    """Roll, then pitch, then yaw - the order an attitude is built in, so the
    three numbers read as the attitude they are."""
    out = []
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    for x, y, z in pts:
        x, y = x * cr - y * sr, x * sr + y * cr                 # roll, about z
        y, z = y * cp - z * sp, y * sp + z * cp                 # pitch, about x
        x, z = x * cy + z * sy, -x * sy + z * cy                # yaw, about y
        out.append((x, y, z))
    return out


class View:
    """A camera. Projecting keeps each point's DEPTH beside its pixel, which
    is what lets the z-buffer in `model` decide what is seen."""

    def __init__(self, yaw, pitch, roll, dist=9.0, margin=3, spin=0):
        self.a = (yaw, pitch, roll)
        self.dist, self.margin = dist, margin
        self.spin = math.radians(spin)              # ...and see below
        self.scale, self.cx, self.cy = 1.0, 0.0, 0.0

    def __call__(self, pts):
        # SPIN is a turn of the finished PICTURE and not of the aeroplane: it
        # changes no foreshortening and no depth, only which way up the
        # composition sits. A plan view has to have one - yaw and pitch put
        # the nose at the bottom of the frame and there is no attitude that
        # lifts it without also stopping the view being a plan.
        cs, sn = math.cos(self.spin), math.sin(self.spin)
        out = []
        for x, y, z in rot(pts, *self.a):
            k = self.scale * self.dist / (self.dist - z)
            px, py = x * k, y * k
            out.append((self.cx + px * cs - py * sn,
                        self.cy - (px * sn + py * cs), z))
        return out

    def fit(self, polys, w, h):
        """Scale and centre the model onto the frame it is drawn in, instead
        of to a constant that has to be re-guessed every time the angle
        moves. The fit is over the projected points, so it is right for any
        attitude without being told anything about the aeroplane."""
        self.scale, self.cx, self.cy = 1.0, 0.0, 0.0
        pts = [p for poly in polys for p in self(poly)]
        x0 = min(p[0] for p in pts); x1 = max(p[0] for p in pts)
        y0 = min(p[1] for p in pts); y1 = max(p[1] for p in pts)
        m = self.margin
        self.scale = min((w - 2 * m) / (x1 - x0), (h - 2 * m) / (y1 - y0))
        self.cx = w / 2.0 - self.scale * (x0 + x1) / 2.0
        self.cy = h / 2.0 - self.scale * (y0 + y1) / 2.0
        return self


def model(c, view, polys, lines=()):
    """Paint a model: every face filled WITH ITS DEPTH into a z-buffer, and
    then every edge drawn only where it is not behind what the fills left.

    Sorting the faces and painting back to front - which is what the
    simulator itself does (SPEC.md 88.5.4) - cannot work here: a fuselage
    panel runs the length of the aeroplane and a wing crosses it, so there
    is no order of those two that is right along the whole of both. The
    simulator takes that trade because it has milliseconds; this is drawn
    once, on a host, where being right is free.

    The fills are white, so the aeroplane hides the cloud behind it exactly
    as the flat drawing's do."""
    view.fit(polys, c.w, c.h)
    zb = [[-1e9] * c.w for _ in range(c.h)]
    faces = [view(p) for p in polys]
    for pts in faces:
        c.fillz(pts, zb)
    for pts in faces:
        for a, b in zip(pts, pts[1:] + pts[:1]):
            c.linez(a, b, zb)
    for seg in lines:                                 # struts and wires, which
        pts = view(seg)                               # are line and not solid
        for a, b in zip(pts, pts[1:]):
            c.linez(a, b, zb)


def box(x0, x1, y0, y1, z0, z1):
    """A thin plate as six quads - a wing, a fin, a tailplane. Thin, but not
    flat: a single plate has no leading edge, and the leading edge is most
    of what says wing when the view is nearly along it."""
    p = [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
         (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]
    return [[p[0], p[1], p[2], p[3]], [p[4], p[5], p[6], p[7]],      # under, over
            [p[0], p[1], p[5], p[4]], [p[3], p[2], p[6], p[7]],      # fore, aft
            [p[0], p[3], p[7], p[4]], [p[1], p[2], p[6], p[5]]]      # the two tips


def tube(stations):
    """A fuselage: hexagonal rings down its length and the panels between
    them, each station (z, half-width, centre y, half-height). SIX sides
    because it is the OUTLINES that are drawn, and six longerons read as a
    round fuselage where four read as a crate - and only a few stations,
    because every extra one is another hoop drawn across the picture."""
    def ring(z, w, yc, h):
        return [(0, yc + h, z), (w, yc + 0.45 * h, z), (w, yc - 0.45 * h, z),
                (0, yc - h, z), (-w, yc - 0.45 * h, z), (-w, yc + 0.45 * h, z)]
    rings = [ring(*st) for st in stations]
    out = []
    for a, b in zip(rings, rings[1:]):
        for i in range(6):
            j = (i + 1) % 6
            out.append([a[i], a[j], b[j], b[i]])
    return out


def prism(profile, x0, x1):
    """A shape given as a profile in the y-z plane, extruded across the span
    from x0 to x1: the two sides, and a quad for each edge of the profile.
    It is `box` with the corners taken off - a rudder is a rounded thing,
    and a rectangle reads as a slab from every angle a box does not."""
    a = [(x0, y, z) for y, z in profile]
    b = [(x1, y, z) for y, z in profile]
    out = [a, b[::-1]]
    for i in range(len(profile)):
        j = (i + 1) % len(profile)
        out.append([a[i], a[j], b[j], b[i]])
    return out


def disc3(centre, r, axis, n=20):
    """A circle standing in the plane normal to `axis`: a propeller, a wheel.
    One polygon, because in a line drawing a disc's outline is a disc."""
    cx, cy, cz = centre
    u, v = {"x": ((0, 1, 0), (0, 0, 1)), "y": ((1, 0, 0), (0, 0, 1)),
            "z": ((1, 0, 0), (0, 1, 0))}[axis]
    return [(cx + r * (u[0] * ca + v[0] * sa), cy + r * (u[1] * ca + v[1] * sa),
             cz + r * (u[2] * ca + v[2] * sa))
            for ca, sa in ((math.cos(2 * math.pi * i / n),
                            math.sin(2 * math.pi * i / n)) for i in range(n))]


def art_c172(c):
    """The Cessna 172 SIDE ON, nose to the left: high wing on a strut, a
    tricycle undercarriage, the cabin glazed all the way round. A side
    elevation is the right view for the trainer - it is the shape a pilot
    knows it by, and it is the one view a flat drawing gets exactly right."""
    body = [(12, 53), (18, 47), (30, 44), (46, 43), (54, 34), (82, 34), (92, 38),
            (118, 42), (140, 17), (147, 17), (150, 46), (150, 51), (130, 55),
            (100, 60), (64, 64), (42, 64), (24, 61), (13, 57)]
    wing = [(30, 29), (108, 29), (108, 33), (30, 33)]
    stab = [(124, 52), (150, 49), (150, 52), (128, 56)]
    spinner = [(7, 53), (13, 49), (13, 57)]
    prop = arc(11, 53, 2, 19, 0, 360)
    nosewheel = arc(31, 71, 4, 5, 0, 360)
    mainwheel = arc(76, 72, 5, 6, 0, 360)
    solids(c, [body, wing, stab, spinner, prop, nosewheel, mainwheel])
    # the strut, the windows, the door, the gear legs, the rudder hinge
    c.line(58, 63, 48, 33)                            # the wing strut
    c.line(60, 63, 50, 33)
    c.polyline([(46, 43), (54, 35), (62, 35), (62, 43), (46, 43)])     # windscreen
    c.polyline([(64, 35), (78, 35), (78, 43), (64, 43), (64, 35)])     # the door's window
    c.polyline([(80, 35), (88, 36), (94, 41), (80, 43), (80, 35)])     # the rear window
    c.line(64, 44, 64, 62)                            # the door
    c.line(64, 62, 78, 62)
    c.line(30, 63, 31, 66)                            # the nose gear leg
    c.line(74, 64, 76, 66)                            # the main gear leg
    c.line(142, 20, 147, 44)                          # the rudder hinge
    c.line(118, 42, 148, 45)                          # the fuselage top under the fin


def art_pitts(c):
    """The Pitts Special S-2B, THREE-QUARTERS from ahead and below, banked
    into a climbing turn - the aerobat seen the way its own flight model
    lets it be flown (`cs_att_free`, SPEC.md 88.7.2), and deliberately not
    the angle the trainer is drawn from.

    From BELOW rather than above, which is the one choice a biplane forces:
    seen from over the top the upper wing simply covers the lower one and
    the picture is a monoplane. From under it they separate, and the bay of
    struts between them is the whole of what says biplane."""
    # The margin is the aeroplane's SIZE in its frame, and it is set so the
    # aerobat sits inside the cloud as the trainer does: a banked
    # three-quarter view is tall where a side elevation is wide, so fitting
    # both to the same frame would draw one twice the size of the other.
    view = View(yaw=-62, pitch=-8, roll=15, margin=11)
    polys = tube([(2.35, 0.27, 0.02, 0.30),           # the cowl's front...
                  (1.45, 0.35, 0.02, 0.41),           # ...and the firewall
                  (-0.60, 0.30, 0.00, 0.36),
                  (-2.60, 0.07, 0.08, 0.13)])         # the sternpost
    polys += box(-1.70, 1.70, 0.92, 1.00, 0.14, 0.94)       # the upper wing...
    polys += box(-1.52, 1.52, -0.46, -0.38, -0.66, 0.10)    # ...ahead of the lower
    polys += prism([(0.10, -1.62), (0.62, -1.80), (1.02, -2.24),      # the fin,
                    (1.10, -2.54), (0.96, -2.80), (0.14, -2.86)],     # rounded
                   -0.04, 0.04)
    polys += prism([(0.02, -2.06), (0.08, -2.10), (0.10, -2.70),      # and the
                    (0.02, -2.84)], -0.88, 0.88)                      # tailplane
    polys += [disc3((0, 0.02, 2.44), 0.16, "z"),            # the spinner
              disc3((0, -0.30, -2.72), 0.11, "x")]          # the tailwheel
    polys += [disc3((sx * 0.62, -1.26, 0.52), 0.24, "x") for sx in (-1, 1)]
    # The propeller is an OUTLINE and not a face: a spinning disc is drawn as
    # the ring it sweeps, and filled it would be a white plate over the nose.
    prop = disc3((0, 0.02, 2.50), 0.56, "z")
    lines = [prop + prop[:1],
             [(-0.26, 0.36, -0.10), (0.26, 0.36, -0.10),    # the open cockpit
              (0.24, 0.34, -0.62), (-0.24, 0.34, -0.62), (-0.26, 0.36, -0.10)],
             [(0, 0.14, -1.66), (0, 1.08, -2.30)]]          # the rudder hinge
    for sx in (-1, 1):
        # ONE cabane a side, not the real aeroplane's pair: four uprights
        # this close together read as a comb at 152 pixels rather than as
        # the strutting they are.
        lines += [[(sx * 0.22, 0.40, 0.62), (sx * 0.26, 0.92, 0.56)],   # cabane
                  [(sx * 1.06, -0.40, -0.08), (sx * 1.12, 0.92, 0.30)], # the bay's
                  [(sx * 1.06, -0.40, -0.54), (sx * 1.12, 0.92, 0.78)], # N-struts
                  [(sx * 1.06, -0.40, -0.54), (sx * 1.12, 0.92, 0.30)], # ...diagonal
                  [(sx * 0.24, -0.44, 0.66), (sx * 0.62, -1.22, 0.52)], # the bowed
                  [(sx * 0.24, -0.44, 0.34), (sx * 0.62, -1.22, 0.52)]] # spring gear
    model(c, view, polys, lines)


def art_fouga(c):
    """The Fouga CM.170 Magister, from BEHIND and above, nose going away to
    the left. The angle is chosen by the aeroplane: its one unmistakable
    feature is the BUTTERFLY TAIL, and a butterfly tail seen from the side
    is a fin - the two panels overlap exactly and the V that names it is
    edge on. From behind they open out.

    Proportioned off the real one, because a wrong ratio is what makes a
    drawing read as a toy: 12.15 m of span to 10.06 m of length, so the
    span is the LONGER of the two, which no other aeroplane here is."""
    # From nearly overhead: a butterfly tail opens into its V in PLAN, which
    # a rear three-quarter promises and does not deliver at this size - the
    # two panels there are a plate and a sliver. `spin` composes it on the
    # diagonal; it turns the picture and not the aeroplane.
    view = View(yaw=-22, pitch=74, roll=0, margin=7, spin=225)
    polys = tube([(2.70, 0.14, 0.10, 0.17),           # the slim nose...
                  (1.80, 0.33, 0.06, 0.42),
                  (0.20, 0.36, 0.00, 0.46),
                  (-1.60, 0.24, 0.02, 0.30),
                  (-2.95, 0.09, 0.06, 0.11)])
    polys += box(-3.20, 3.20, -0.08, 0.00, -0.58, 0.46)     # the straight wing
    for sx in (-1, 1):                                      # ...ending in a
        polys += box(sx * 2.94, sx * 3.24, -0.22, 0.10, -0.62, 0.52)   # tank
    # THE BUTTERFLY: two panels at 40 degrees, written flat and ROLLED about
    # the fuselage axis, which is what makes the V a V rather than two
    # guesses at where a corner goes.
    for sx in (-1, 1):
        polys += [rot(p, 0, 0, sx * 40)
                  for p in box(0.0, sx * 1.30, -0.03, 0.03, -2.90, -2.06)]
    lines = [[(-0.28, 0.44, 1.42), (0.28, 0.44, 1.42),      # the long canopy
              (0.32, 0.36, 0.10), (-0.32, 0.36, 0.10), (-0.28, 0.44, 1.42)],
             [(-0.30, 0.40, 0.76), (0.30, 0.40, 0.76)],     # ...two in tandem
             [(0, 0.46, 1.42), (0, 0.44, 0.10)]]
    for sx in (-1, 1):
        lines += [[(sx * 0.36, 0.20, 0.30), (sx * 0.36, 0.20, -0.10),  # the
                   (sx * 0.36, -0.02, -0.16), (sx * 0.36, -0.02, 0.26)],
                  [(sx * 2.20, -0.04, -0.52), (sx * 2.20, -0.04, 0.40)]]  # aileron
    model(c, view, polys, lines)


def art_bijave(c):
    """The Wassmer WA-30 Bijave, nearly PLAN, from well above and banked. A
    sailplane's identity is its SPAN - sixteen metres against a fuselage of
    seven - and a side elevation is the one view that hides all of it: seen
    from the side a glider is a thin tube. From above it is a wing with an
    aeroplane hung under it, which is what it is."""
    # Nearly HEAD ON and banked, which is the view that has the span in it:
    # from the side a glider is a thin tube with a stick behind it, and from
    # directly above the wing is right but the aeroplane is a plan drawing.
    # Banked and coming at you, the sixteen metres cross the whole frame.
    view = View(yaw=-26, pitch=-14, roll=20, margin=6)
    polys = tube([(2.30, 0.16, 0.06, 0.20),           # a slender pod...
                  (1.30, 0.30, 0.02, 0.36),
                  (0.20, 0.26, 0.00, 0.32),
                  (-1.90, 0.11, 0.04, 0.13),
                  (-3.10, 0.06, 0.06, 0.08)])         # ...and a long boom
    # The wing in three panels a side: a straight centre and two outer ones
    # cranked up, which is the Bijave's own gull and is also what stops a
    # 5.8-unit span reading as a plank.
    polys += box(-1.90, 1.90, 0.30, 0.36, -0.30, 0.66)
    for sx in (-1, 1):
        polys += box(sx * 1.88, sx * 3.60, 0.36, 0.42, -0.22, 0.58)
        polys += box(sx * 3.58, sx * 5.05, 0.44, 0.49, -0.10, 0.44)
    polys += box(-1.28, 1.28, 0.62, 0.67, -2.72, -2.28)     # the T-tail, up...
    polys += box(-0.04, 0.04, 0.08, 0.66, -2.94, -2.16)     # ...on its fin
    lines = [[(-0.28, 0.34, 1.42), (0.28, 0.34, 1.42),      # the long canopy
              (0.30, 0.28, 0.10), (-0.30, 0.28, 0.10), (-0.28, 0.34, 1.42)],
             [(-0.29, 0.31, 0.74), (0.29, 0.31, 0.74)],     # ...two in tandem
             [(0, 0.37, 1.42), (0, 0.32, 0.10)],
             [(-1.90, 0.33, 0.20), (1.90, 0.33, 0.20)]]     # the spoiler line
    model(c, view, polys, lines)


def art_a5(c):
    """The Icon A5, from ahead and low, NOSE TO THE RIGHT - the only one here
    that faces the other way, which is itself half of telling it apart at a
    glance. Low, because what makes it an amphibian is the HULL: the
    planing bottom and the step are under the waterline in every other view,
    and its wing and pusher propeller are shared with any number of light
    aeroplanes."""
    view = View(yaw=64, pitch=4, roll=-8, margin=8)
    # The hull, whose bottom is a V rather than a tube's rounded one - the
    # planing bottom is the whole point, so it is a station list of its own.
    def hull(z, w, yc, h, keel):
        return [(0, yc + h, z), (w, yc + 0.5 * h, z), (w, yc - 0.2 * h, z),
                (0, yc - h - keel, z), (-w, yc - 0.2 * h, z), (-w, yc + 0.5 * h, z)]
    st = [(2.30, 0.20, 0.06, 0.26, 0.06), (1.55, 0.44, 0.02, 0.42, 0.20),
          (0.30, 0.48, 0.00, 0.44, 0.26), (-0.55, 0.40, 0.06, 0.34, 0.24),
          (-0.60, 0.34, 0.14, 0.26, 0.02),                  # THE STEP
          (-2.30, 0.18, 0.24, 0.16, 0.02)]
    rings = [hull(*s) for s in st]
    polys = []
    for a, b in zip(rings, rings[1:]):
        polys += [[a[i], a[(i + 1) % 6], b[(i + 1) % 6], b[i]] for i in range(6)]
    polys += box(-3.10, 3.10, 0.86, 0.94, -0.42, 0.62)      # the high wing...
    for sx in (-1, 1):                                      # ...and a sponson
        polys += box(sx * 0.62, sx * 1.36, -0.12, -0.02, -0.10, 0.52)
    polys += box(-1.32, 1.32, 0.44, 0.50, -2.40, -1.92)     # the tailplane...
    polys += box(-0.04, 0.04, 0.10, 0.52, -2.46, -1.66)     # ...on its fin
    polys += [disc3((0, 0.74, -1.06), 0.62, "z")]           # the pusher's hub
    lines = [disc3((0, 0.74, -1.02), 0.86, "z") + [(0.86, 0.74, -1.02)],
             [(-0.44, 0.50, 1.62), (0.44, 0.50, 1.62),      # the wide canopy
              (0.50, 0.86, 0.30), (-0.50, 0.86, 0.30), (-0.44, 0.50, 1.62)],
             [(-0.48, 0.68, 0.96), (0.48, 0.68, 0.96)],
             [(-0.50, -0.44, 0.42), (0.50, -0.44, 0.42)]]   # the step, seen
    for sx in (-1, 1):
        lines += [[(sx * 0.42, 0.86, 0.10), (sx * 0.42, 0.62, -0.86)]]  # pylon
    model(c, view, polys, lines)


# =============================================================================
# The title: "Clear Skies" in an outlined italic script, 272 x 40
# =============================================================================
ART_TITLE_W, ART_TITLE_H = 272, 40
SHEAR = 0.28                                          # x per row above the baseline
BASE = 33                                             # the baseline row
CAP, XH = 30, 19                                      # cap height and x-height


def glyphs():
    """Each glyph: (advance, [strokes]), a stroke a list of points in a box
    whose baseline is y = 0 and whose top is y = -CAP (up is negative)."""
    g = {}
    g["C"] = (24, [arc(13, -15, 12, 15, 40, 320)])
    g["l"] = (11, [[(3, -CAP), (3, -6), (5, -1), (10, -2)]])
    g["e"] = (19, [[(2, -10), (16, -10)] + arc(9, -10, 7.5, 9.5, 0, -250)])
    g["a"] = (20, [arc(9, -9.5, 7.5, 9.5, 0, 360), [(17, -XH), (17, -3), (20, -1)]])
    g["r"] = (14, [[(3, -XH), (3, 0)], [(3, -12)] + arc(9, -12, 6, 7, 180, 300)])
    g["S"] = (22, [arc(12, -22, 9, 8, 340, 90) + arc(12, -8, 10, 8, 270, 510)])
    g["k"] = (18, [[(3, -CAP), (3, 0)], [(3, -8), (7, -10), (14, -XH)], [(7, -11), (16, 0)]])
    g["i"] = (9, [[(3, -XH), (3, -3), (5, -1), (8, -2)], arc(3, -25, 1.6, 1.6, 0, 360)])
    g["s"] = (17, [arc(9, -14, 6.5, 5, 340, 90) + arc(9, -5, 7, 5, 270, 510)])
    g[" "] = (12, [])
    return g


def title_art():
    c = Canvas(ART_TITLE_W, ART_TITLE_H)
    g = glyphs()
    text = "Clear Skies"
    x = 6
    strokes = []
    for ch in text:
        adv, sts = g[ch]
        for st in sts:
            strokes.append([(x + px + SHEAR * (-py), BASE + py) for px, py in st])
        x += adv
    for st in strokes:                                # the black body...
        c.stroke(st, 2.6, 1)
    for st in strokes:                                # ...and the white centre
        c.stroke(st, 1.3, 0)
    return c


def emit(path, title, planes):
    """Write the include: ONE LZ4 STREAM and the offsets into what it expands
    to (SPEC.md 88.10.2).

    The six bands are 10,480 bytes of a 60KB segment and they are the largest
    block in the package that no frame ever reads - the title page draws them
    and the fsx bracket never does. So they are stored PACKED and the package
    expands them once, at launch, into a heap claim (skies.asm's cs_artload).
    A band is a bitmap and holds no pointer, so nothing needs relocating: what
    was a label is an offset into the blob, and `dw cs_art_c172` in a plane
    record goes on assembling because an equ is a constant like any other.

    They pack as well as they do because every aircraft is drawn in front of
    the SAME cumulus (see plane_art above), so band 2 onwards is mostly a
    match into band 1 - which is the one case LZ4's 64KB window was made for.
    """
    lines = ["; CLEAR SKIES' title-screen art (SPEC.md 88.10): GENERATED by",
             "; tools/csart.py - do not edit by hand; tests/unit/t_csart.py holds",
             "; this file to the generator. Each band is OSAPI_GFX_BLIT1's own",
             "; order: row-major, bit 7 leftmost, a set bit LIT - so ink is a",
             "; clear bit and paper a set one, on every adapter, with no pen.",
             "; The aircraft bands share ONE frame and a plane record names",
             "; the one it is drawn from in CSP_ART (SPEC.md 88.10.1).",
             ";",
             "; THE BANDS ARE PACKED (SPEC.md 88.10.2). What follows the equs is",
             "; ONE LZ4 stream, T word first, that expands to CS_ART_SIZE bytes:",
             "; the title band then the five aircraft, in CSP_ART's order. The",
             "; names below are OFFSETS INTO THAT BLOB and not labels in this",
             "; segment - cs_artload claims CS_ART_KB and hands the whole thing",
             "; to OSAPI_DECOMP once, and every blit reads [cs_artseg]:offset.",
             ""]

    bands = [title] + list(planes)
    blob = b""
    offs = []
    for name, canvas in bands:
        offs.append((name, len(blob)))
        for row in canvas.band():
            blob += bytes(row)

    # ONE frame for every aircraft: cs_paint blits whichever band the plane
    # in use names, with the one width and height, so a band of another size
    # would draw the wrong picture rather than fail to assemble.
    frames = set((c.w, c.h) for _, c in planes)
    assert len(frames) == 1, "the aircraft bands share one frame: %r" % (frames,)

    z = os88lz.compress(blob, os88lz.LZ4)
    # The gate every generated stream owes its reader: it is not enough that
    # the compressor ran, the bytes have to come back (SPEC.md 88.10.2).
    assert os88lz.decompress(z, os88lz.LZ4, len(blob)) == blob, \
        "csart: the stream does not expand to the bands that made it"
    assert len(z) < len(blob), "csart: the stream is not smaller than the bands"

    lines.append("cs_art_title_w equ %d" % title[1].w)
    lines.append("cs_art_title_h equ %d" % title[1].h)
    lines.append("cs_art_plane_w equ %d" % planes[0][1].w)
    lines.append("cs_art_plane_h equ %d" % planes[0][1].h)
    lines.append("")
    for name, off in offs:
        lines.append("%-16s equ %d" % (name, off))
    lines.append("")
    lines.append("CS_ART_SIZE equ %d        ; what the stream expands to" % len(blob))
    lines.append("CS_ART_ZLEN equ %d         ; ...out of this many, T word in" % len(z))
    lines.append("CS_ART_KB   equ %d            ; ...into a claim of this many KB"
                 % ((len(blob) + 1023) // 1024))
    lines.append("")
    lines.append("cs_art_z:")
    for i in range(0, len(z), 24):
        lines.append("    db " + ", ".join("0x%02X" % b for b in z[i:i + 24]))
    lines.append("")
    open(path, "w").write("\n".join(lines))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "apps", "skies", "csart.inc"))
    ap.add_argument("--preview", help="write PNG previews into this directory")
    ap.add_argument("--zoom", type=int, default=4)
    a = ap.parse_args(argv)
    title = ("cs_art_title", title_art())
    planes = [("cs_art_c172", plane_art(art_c172)),      # in cs_planes' order,
              ("cs_art_pitts", plane_art(art_pitts)),    # and CSP_ART's
              ("cs_art_fouga", plane_art(art_fouga)),
              ("cs_art_bijave", plane_art(art_bijave)),
              ("cs_art_a5", plane_art(art_a5))]
    bands = [title] + planes
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        for name, canvas in bands:
            canvas.png(os.path.join(a.preview, name + ".png"), a.zoom)
    emit(a.out, title, planes)
    print("csart: %s (%s)" % (a.out, ", ".join("%s %dx%d" % (n, c.w, c.h) for n, c in bands)))


if __name__ == "__main__":
    main(sys.argv[1:])
