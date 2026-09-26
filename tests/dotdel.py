#!/usr/bin/env python3
"""DOT DELIRIUM on the glass: the attract screen, a game, and the bracket.

Five questions, and each one has gone wrong at least once during the build
(SPEC.md 93):

  A  THE TITLE SCREEN DRAWS.  Its own lettering, the saved table, the play
     line and a strip of playfield with actors in it - which is four separate
     compositors and any of them can come up empty without erroring.
  B  THE PLAY LINE BLINKS.  Two captures a beat apart differ in that line's
     band and NOWHERE ELSE outside the playfield strip.
  C  ENTER STARTS A GAME, and Smiles then EATS: the dot count falls and the
     score rises. A maze chase whose dots never go is a maze chase that never
     ends.
  K  ACROSS AND DOWN ARE THE SAME SPEED ON THE GLASS (SPEC.md 93.7.6).  A
     tile is not square on any adapter - 1.15 : 1 on a Hercules, 1.23 on a
     VGA, 0.83 on a CGA - so one tile in DD_TILET ticks EITHER WAY makes an
     actor 15-30% faster along its longer axis, which the field read
     straight off the screen.  Checked PER SPEED, because the failure it
     caught was rounding and that only shows where the steps are smallest.
  D  THE BOARD IS CUT FROM THE SURFACE (SPEC.md 93.3).  The tile is what the
     adapter's own pixel shape and the live content box say it should be, and
     going fullscreen re-cuts it BIGGER and leaving puts it back.
  F  EVERY DOT IS DRAWN IN ITS OWN TILE, and the title has a title in it
     (SPEC.md 93.5.9.1).  The grid comes out of the guest and the pixels off
     the glass, so nothing has to be compared against a second build: a dot
     the grid puts at (c, r) must put ink inside tile (c, r), and the title's
     ink must span its own box rather than pile up at one edge of it.
     `dd_band_rect`'s fast path computed the BIT offset of a run and not its
     BYTE column, so every rectangle landed at the left edge of its band -
     the title's cells all in byte column 0, every dot in the band's leftmost
     tile.  An exhaustive host-side model of the arithmetic passed 800 cases
     against that build, because the model was written from the DESIGN and
     the byte column was missing only from the CODE.
  E  THE FRAME IS THE TICK (SPEC.md 93.6).  A window that a WHOLE REPAINT
     fell into is taken again: that is 677 ms on a VGA and SPEC.md 93.5.3.1
     names it as one of the two costs that are not per frame, so counting it
     as one made this leg read anywhere from 82.7% to 99.7% on a single
     build.  Rendered frames per guest second
     against the game's own tick counter, on a cycle-accurate 4.77 MHz 8088.
     This is the row's reason for existing: three separate things - a board
     walk, a `font_run` and a pair of divides - each took it to 60% while
     everything still LOOKED right (SPEC.md 93.5.3).

BREAK IT ON PURPOSE: set `[dd_stpy + bx]` from a base speed the way it was -
`th * DD_SUB / DD_TILET`, scaled by the percentage - and leg K reads 1.15x on
the Hercules arm, 1.23x on the VGA and 0.83x on the CGA.  Convert a base speed
once instead of converting each of the ten, and only the CGA's frightened row
goes red, at 1.13x.  Put `dd_pills_blit` back on a board walk and leg E goes
red at ~63%, and so does copying the wall picture into every actor's band
(SPEC.md 93.5.3 item 4, which cost 12 ms of a 54.9 ms frame). What this row
does NOT read is a wrong COLOUR or a dot drawn half - those are a look, and
SPEC.md 93.5.1 and 93.5.4 are where they are written down.

DOT DELIRIUM RIDES THE ORDINARY APPS DISK at every geometry (SPEC.md 93.13):
360KB fits it at 352 of 354 clusters, which is what taking the old Pac-Man
port off that disk bought.
"""
import argparse
import os
import struct
import tempfile
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88build                                            # noqa: E402
import os88geom as G                                        # noqa: E402
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

PKG = "B:/GAMES/DOTDEL.O88"

# The three adapters, with the tile SPEC.md 93.3's table says each should get
# in a window on a 360KB machine. VGA is an XT with a VGA card; the two 1bpp
# ones are 5150s with the GLaBIOS twin, because the IBM ROM is not in the tree.
# ...and the floor leg E fails under, PER ARM. See FPS_FLOOR below.
ARMS = (
    # VGA's floor is 0.90 again. It was 0.80 for as long as SPEC.md 93.5.13.3's
    # split cost ten points - six samples read 83.6 to 88.7 - and that cost is
    # gone twice over: 5.4.2.6's gfx_blit1 fast path took the one-pen frame
    # back to 96-99%, and 93.5.19's planar band retires the split wherever a
    # colour surface is uncovered. Ten eight-second windows of the planar
    # build read 98.7-100.0 windowed and 98.8-100.0 fullscreen, and this row
    # read 99.1 / 98.7. 0.90 is the floor this arm had before the split, and it
    # still sits well over what a real regression does (93.5.3's 60s, the 78.0
    # that caught 93.5.13.2, and the 76 a whole band through the old
    # gfx_blitp row loop read - 93.5.19).
    # THE BRANCH'S TILES (SPEC.md 93.3.3): Window > Thin is the default, so the
    # tile is square-ish in PIXELS and the board is the arcade's 28:31 rather
    # than 1.6x wider than tall.  CGA is the one that cannot reach it - 640x200
    # has not got the 279 lines that 31 rows of nine need - so it resolves to
    # Full with Thin greyed, and comes out at what fits.
    ("vga",  "os8088_xt_vga",        (8, 9),  0.90),
    ("cga",  "os8088_5150_cga_gla",  (8, 4),   0.95),
    ("herc", "os8088_5150_herc_gla", (8, 9),  0.95),
)

TICK_HZ = 18.2065
CPU_HZ = 4772727.0
# THE HEADROOM IS NOT THE SAME ON EVERY ARM, and one number for all three was
# a number nobody had measured: SPEC.md 93.5.3's 98-100% is the DEMO playing
# itself, and this leg steers a real game.
#
# Measured, steered, on the same instrument. CGA and Hercules read 98.4 to
# 100.2% windowed and fullscreen, run after run, with a whole tick to spare.
#
# VGA WINDOWED IS THE HEAVIEST CASE IN THE TREE - 448x403, a 16x13 tile, five
# actor bands of a 54.9 ms tick - and it used to sit apart: 91.7 / 93.2 / 95.1
# alone and 88.9 / 89.8 sharing four cores with two other guests, which is a
# distribution a 90% floor sat INSIDE. It carried an 85% floor of its own for
# exactly one round, on the note that it would come back up when SPEC.md
# 93.5.3.1's band composition was taken.
#
# It was, and it did - a band is 6.16 -> 4.69 ms - and the arm read 99.1 /
# 99.7 / 99.7 windowed, so all three went to 0.95.
#
# THOSE THREE WERE THE TOP OF A SPREAD, and setting a floor from them was the
# very mistake the paragraph above describes. With the window now retaken when
# a whole repaint falls in it - which was itself hiding four points of noise -
# the same arm reads 94.3 / 92.9 / 98.5: a mean of 95.2 and a spread of 5.6,
# sitting ACROSS 0.95. Three samples is not a distribution, and three samples
# that agree are the easiest kind to believe.
#
# So VGA is 0.90, 2.9 points under the worst clean reading, and CGA and
# Hercules keep 0.95 on a spread of 98.4-100.2 that has held for six rounds.
# The colour arm is not slower for a reason anyone has to fix: SPEC.md
# 93.5.10's repair queue costs it 0.4 to 1.7 points, measured by an A/B in ONE
# session - the same build and scene, a `ret` poked over the routine and back
# - which is a twentieth of the spread and not what puts the arm here.
FPS_FLOOR = 0.95                # ...the default, for an arm that names none


def bss(path="apps/dotdel/dotdel.asm"):
    """The package's .bss offsets, from the source rather than from a copy.

    The names are `equ os88_image_end + N`, so a probe build that emits each
    one MINUS os88_image_end gives N whatever else it changes about the image.
    """
    import re
    import subprocess
    import tempfile
    src = open(os.path.join(ROOT, path)).read()
    names = re.findall(r"^\s+(?:DWORDV|DBYTEV|DBUFV)\s+(\w+)", src, re.M)
    probe = src.replace("    OS88_IMAGE_END", "") + "\ndd_probe:\n"
    for n in names:
        probe += "    dw %s - os88_image_end\n" % n
    probe += "    OS88_IMAGE_END\n"
    with tempfile.TemporaryDirectory() as td:
        asm = os.path.join(td, "probe.asm")
        binf = os.path.join(td, "probe.bin")
        open(asm, "w").write(probe)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/dotdel/", "-o", binf, asm],
                       cwd=ROOT, check=True)
        d = open(binf, "rb").read()
    vals = struct.unpack("<%dH" % len(names), d[len(d) - 2 * len(names):])
    img = struct.unpack("<H", open(os88build.at("build/dotdel.bin"),
                                   "rb").read()[8:10])[0]
    return {n: img + v for n, v in zip(names, vals)}


class Probe(object):
    """The package's own state, read out of its segment."""

    def __init__(self, ui, names, title="Dot Delirium"):
        self.m, self.names = ui.m, names
        w = ui.window(title)
        raw = bytes(self.m.read(G._sym(self.m, None)("wm_wins"),
                                G.MAX_WIN * G.WIN_SIZE))
        self.seg = struct.unpack_from("<H", raw, w.i * G.WIN_SIZE + G.W_SEG)[0]
        if not self.seg:
            raise RuntimeError("no package segment for %r" % title)

    def w(self, n, i=0):
        a = (self.seg << 4) + self.names[n] + i * 2
        return struct.unpack("<H", bytes(self.m.read(a, 2)))[0]

    def b(self, n, i=0):
        return bytes(self.m.read((self.seg << 4) + self.names[n] + i, 1))[0]


def screen(m):
    """(w, h, bytes-per-pixel-triple) of the glass, whichever card it is."""
    v = m.video()
    if v["type"] == "vga" or os88marty.video_is_text(v):
        w, h, data = m.fbuf(0)
        return w, h, bytes(data)
    w, h, rows = m.vram(None)
    return w, h, b"".join(bytes(r) for r in rows)


TT_WALL, TT_DOT, TT_PILL, TT_DOOR = 0, 2, 3, 4
G_COLS, G_ROWS = 28, 31          # DD_COLS x DD_ROWS, the classic grid


def _lit(d, w, h, per, x0, y0, x1, y1):
    for y in range(max(y0, 0), min(y1, h)):
        base = y * w
        for x in range(max(x0, 0), min(x1, w)):
            o = (base + x) * per
            if any(d[o:o + per]):
                return True
    return False


def codeoff(name):
    """The org-0 offset of a CODE label, the way dotdel.bss() reads a bss one.

    The package is assembled at org 0 into one flat binary, so a label's
    offset is what nasm emits for `dw <label>` - the same trick bss() plays,
    without os88_image_end's bias.
    """
    src = open(os.path.join(ROOT, "apps/dotdel/dotdel.asm")).read()
    probe = (src.replace("    OS88_IMAGE_END", "")
             + "\ndd_cprobe:\n    dw %s\n    OS88_IMAGE_END\n" % name)
    with tempfile.TemporaryDirectory() as td:
        asm = os.path.join(td, "probe.asm")
        binf = os.path.join(td, "probe.bin")
        open(asm, "w").write(probe)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/dotdel/", "-o", binf, asm],
                       cwd=ROOT, check=True)
        return struct.unpack("<H", open(binf, "rb").read()[-2:])[0]


def leg_f(tag, ui, p, say):
    """Every dot in its own tile, and a title that spans its own box."""
    m = ui.m
    fail = []
    m.pause()
    cx, cw = p.w("dd_cx"), p.w("dd_cw")
    tity, tith, titw = p.w("dd_tity"), p.w("dd_tith"), p.w("dd_titw")
    m.go()
    titx = ((cx + (cw - titw) // 2) + 4) & ~7
    w, h, d = screen(m)
    per = len(d) // (w * h)
    cols = [x for x in range(titx, titx + titw)
            if _lit(d, w, h, per, x, tity, x + 1, tity + tith)]
    span = (max(cols) - min(cols) + 1) if cols else 0
    if span <= titw * 3 // 4:
        fail.append("%s: the title's ink spans %d px of its %d-px box - it has "
                    "collapsed towards one edge (SPEC.md 93.5.9.1)"
                    % (tag, span, titw))
    else:
        say("%s: title spans %d of %d px" % (tag, span, titw))

    # ...and now the board. FREEZE FIRST and pin the blink LIT: the grid and
    # the pixels have to be of one instant (Smiles eats between two reads),
    # and a pellet caught mid-blink reads exactly like a misplaced one.
    # dd_pilt counts down with `jns`, so it is SIGNED and 250 is -6.
    seg = p.seg
    m.key("Enter")
    # ...until the game has its board: dd_level_begin sets READY and asks
    # for a whole-board frame, and that frame clearing dd_full is the board
    # being on the glass. A blind 18 guest seconds before.
    try:
        os88marty.until(m, lambda _: p.b("dd_state") != 0
                        and p.b("dd_full") == 0,
                        "the game's first board", poll=0.1, limit=10)
    except os88marty.MartyError:
        pass                            # ...the forced frame below says so
    m.pause()
    for n, v in (("dd_paused", 1), ("dd_full", 1)):
        m.write((seg << 4) + p.names[n], bytes([v]))
    m.go()
    # dd_full is cleared by the frame that has just drawn the whole board
    os88marty.until(m, lambda _: p.b("dd_full") == 0,
                    "the forced whole-board frame", poll=0.1, limit=10)
    m.pause()
    tw, th = p.w("dd_tw"), p.w("dd_th")
    bdx, bdy = p.w("dd_bdx"), p.w("dd_bdy")
    grid = bytes(m.read((seg << 4) + p.names["dd_grid"], G_COLS * G_ROWS))
    act = [(p.b("dd_ac", i), p.b("dd_ar", i)) for i in range(5)]
    w, h, d = screen(m)
    m.go()
    per = len(d) // (w * h)
    near = {(c + dc, r + dr) for c, r in act
            for dc in (-1, 0, 1) for dr in (-1, 0, 1)}
    missing = []
    checked = 0
    for r in range(G_ROWS):
        for c in range(G_COLS):
            # DOTS ONLY. A pellet BLINKS, and pinning it lit cannot be made
            # reliable from the host: dd_pilt is a signed byte, so its ceiling
            # is 127 ticks, and 1.5 host seconds is up to ~120 of them when the
            # guest runs 4.4x real time. The four of them prove nothing here
            # that 226 dots do not - both go through dd_band_rect - and
            # tests/dotdelpen.py leg A is where the pellet refresh is read.
            if grid[r * G_COLS + c] != TT_DOT:
                continue
            if (c, r) in near:
                continue                # an actor may be standing on it
            checked += 1
            x0, y0 = bdx + c * tw, bdy + r * th
            if not _lit(d, w, h, per, x0 + 1, y0 + 1, x0 + tw - 1, y0 + th - 1):
                missing.append((c, r))
    if missing:
        fail.append("%s: %d of %d dots have no ink in their own tile - the "
                    "first few at %s (SPEC.md 93.5.9.1)"
                    % (tag, len(missing), checked, missing[:6]))
    else:
        say("%s: %d dots, every one in its own tile" % (tag, checked))
    m.pause()
    m.write((seg << 4) + p.names["dd_paused"], bytes([0]))
    m.go()
    m.key("Escape")                     # ...and hand the attract screen back:
    try:                                # legs B and C both start from it
        os88marty.until(m, lambda _: p.b("dd_state") == 0
                        and p.b("dd_full") == 0,
                        "the attract screen to come back", poll=0.2, limit=10)
    except os88marty.MartyError:
        pass                            # ...and the line below says so
    if p.b("dd_state") != 0:
        fail.append("%s: Escape did not return to the attract screen "
                    "(state %d), so the legs after this one start from the "
                    "wrong place" % (tag, p.b("dd_state")))
    return fail


def leg_g(tag, ui, p, say):
    """Every pixel the BOARD PICTURE says is ink is still on the glass.

    THE GROUND TRUTH IS dd_bdseg - the 1bpp board picture every band's wall
    arm is copied out of - so this asks the one question a colour census
    cannot: not "is that wall the wrong colour" but "is that wall THERE".
    tests/dotdel.py's own leg F and the colour census both skip a tile with no
    lit pixel in it, because most wall tiles legitimately have none (dd_walls_of
    draws every line OUTSIDE the corridor it outlines), and a blacked corner
    looks exactly like one of those.

    IT WALKS EVERY TILE AND NOT THE WALLS, and that is a third defect it
    caught. It used to filter the grid to TT_WALL and TT_DOOR, on the same
    assumption dd_tile_put and dd_band_ground were both making - that a
    CORRIDOR tile holds no ink. SPEC.md 93.2.3's concave round put a corner
    block one line width inside one, and this leg looked straight past 6 of
    the board's 34 concave corners going black inside a second of play, and
    past a power pellet blinking its own corner away four times over. The
    picture is the truth for every tile; the type was never part of the claim.

    BREAK IT ON PURPOSE, two ways, and both of them shipped:

      * Take dd_tile_put's wall arm out - the `.wall` branch that calls
        dd_band_walls - and the maze loses a corner every time an actor turns
        on one: 108 ink pixels in 2 tiles, measured on a VGA over twenty
        seconds of play.  SPEC.md 93.5.10's repair queue hands dd_tile_put
        the corner tile of every turn, and a zeroed band is a corner off the
        glass for the rest of the level ("corners are back to disappearing").
      * Take the dd_ov_spill call out of dd_overlay's erase and the CGA arm
        reads 48 gone in 6 tiles - the whole top pixel row of the wall under
        the ghost house.  The overlay's band is 8 rows and a CGA tile is 4.

    It plays 145 of the game's own ticks first, which is what buys the turns:
    a corner only enters a band when somebody turns on it, so this leg is as
    thorough as the play it watched and no more (which is why the wait is on
    dd_anim and not on the host clock).

    Neither is visible on the other two arms, which is why this runs on all
    three: the queue is gated off below 2 bpp and the spill needs a tile
    shorter than the band.
    """
    m = ui.m
    fail = []
    m.pause()
    m.write((p.seg << 4) + p.names["dd_lives"], bytes([99]))
    st = p.b("dd_state")
    m.go()
    if st == 0:                         # a game that ended: start another
        m.key("Enter")
        try:
            os88marty.until(m, lambda _: p.b("dd_state") != 0,
                            "a new game to start", poll=0.2, limit=20)
        except os88marty.MartyError:
            pass
    # ...and let the cast cross some corners, over the GAME'S OWN CLOCK. A
    # host sleep here hands a loaded lane a third less play (incident 54), and
    # what this leg wants is TURNS TAKEN - the corner tile only enters a band
    # when an actor turns on it, so a lane that got fewer of them is a lane
    # that read less of the maze while reporting the same number.
    t0 = p.w("dd_anim")
    try:
        os88marty.until(m, lambda _: (p.w("dd_anim") - t0) & 0xFFFF >= 145,
                        "145 ticks of play", poll=0.25, limit=60)
    except os88marty.MartyError:
        pass
    ticks = (p.w("dd_anim") - t0) & 0xFFFF
    m.pause()
    tw, th = p.w("dd_tw"), p.w("dd_th")
    bdx, bdy = p.w("dd_bdx"), p.w("dd_bdy")
    sb, bdseg, mh = p.w("dd_sb"), p.w("dd_bdseg"), p.w("dd_mh")
    ovw, ovx, ovy = p.w("dd_ovw"), p.w("dd_ovx"), p.w("dd_ovy")
    grid = bytes(m.read((p.seg << 4) + p.names["dd_grid"], G_COLS * G_ROWS))
    pic = bytes(m.read(bdseg << 4, sb * mh))
    act = [(p.b("dd_ac", i), p.b("dd_ar", i)) for i in range(5)]
    w, h, d = screen(m)
    m.go()
    per = len(d) // (w * h)
    # AN ACTOR IS ALLOWED TO COVER A WALL, and so is a banner that is still up
    skip = set((c + dc, r + dr) for c, r in act
               for dc in (-2, -1, 0, 1, 2) for dr in (-2, -1, 0, 1, 2))
    if ovw:
        for r in range((ovy - bdy) // th, (ovy - bdy + 7) // th + 1):
            for c in range((ovx - bdx) // tw, (ovx - bdx + ovw - 1) // tw + 1):
                skip.add((c, r))
    tot = miss = nt = 0
    bad = []
    for r in range(G_ROWS):
        for c in range(G_COLS):
            if (c, r) in skip:
                continue
            nt += 1
            here = 0
            for y in range(r * th, (r + 1) * th):
                if bdy + y >= h:
                    break
                row = y * sb
                for x in range(c * tw, (c + 1) * tw):
                    if not (pic[row + (x >> 3)] >> (7 - (x & 7))) & 1:
                        continue
                    tot += 1
                    o = ((bdy + y) * w + (bdx + x)) * per
                    if not any(d[o:o + per]):
                        miss += 1
                        here += 1
            if here:
                bad.append((c, r))
    if not tot:
        fail.append("%s: the board picture has no ink at all in %d "
                    "tiles - this leg read nothing" % (tag, G_COLS * G_ROWS))
    elif miss:
        fail.append("%s: %d of %d board-ink pixels are BLACK on the glass, in "
                    "%d of %d tiles %s - something drew over the maze and put "
                    "back an empty tile (SPEC.md 93.5.10, 93.5.11) "
                    "[state %d tile %dx%d board %dx%d at %d,%d ovw %d "
                    "actors %s]"
                    % (tag, miss, tot, len(bad), nt, bad[:8], p.b("dd_state"),
                       tw, th, p.w("dd_mw"), mh, bdx, bdy, ovw, act))
    elif ticks < 145:
        fail.append("%s: the game's own clock advanced %d ticks in 60 host "
                    "seconds - the guest is not running, so an intact maze "
                    "here proves nothing" % (tag, ticks))
    else:
        say("%s: all %d board-ink pixels over %d tiles still on the glass, "
            "after %d ticks of play" % (tag, tot, nt, ticks))
    return fail


def leg_h(tag, ui, p, say, frames=20, cap=5):
    """No WALL tile is left in the wrong pen at the END of a frame.

    A band goes down in ONE pen, so every tile in it wears the actor's ink.
    For a tile the actor is standing on that is the accepted price of 93.5.1 -
    the field called it fine - and for the maze's own corner, which the union
    of two boxes covers when they differ on both axes and neither box is on,
    it is a bright flash on something nothing was ever standing on.

    SPEC.md 93.5.13 queues that corner and 93.5.10's drain puts it back on the
    NEXT frame, so what this asserts is not "never wrong" but "never wrong for
    long": a reading is taken at dd_draw's entry, where the previous frame is
    finished, and no single tile may be wrong in more than `cap` of them.

    It excludes only tiles an actor's BOX is on - not a ring of two, which is
    what the colour census does and why the census could never see this at
    all: the corner is adjacent to the actor by construction.

    BREAK IT ON PURPOSE: poke `clc / ret` (F8 C3) over dd_split_ck so the
    corner is never queued.  It then stays wrong until the actor is two tiles
    away and this reads the same tile wrong in a dozen consecutive frames.
    Put dd_rep_covered's ring-of-one test back and it goes red the same way,
    that ring being what deferred the repair for as long as anybody was near.

    VGA ONLY: one plane has no pen at all (SPEC.md 5.4.2.2).
    """
    m = ui.m
    fail = []
    if not settle_playing(m, p):
        say("%s: not playing - leg H skipped its sample" % tag)
        return fail
    m.pause()
    tw, th = p.w("dd_tw"), p.w("dd_th")
    bdx, bdy = p.w("dd_bdx"), p.w("dd_bdy")
    sb, bdseg, mh = p.w("dd_sb"), p.w("dd_bdseg"), p.w("dd_mh")
    pic = bytes(m.read(bdseg << 4, sb * mh))
    m.breakpoints([{"type": "execseg", "seg": p.seg,
                    "off": codeoff("dd_draw")}])
    wrong = {}
    seen = took = 0
    for _ in range(frames):
        m.go()
        if not m.wait_stop(20.0):
            break
        grid = bytes(m.read((p.seg << 4) + p.names["dd_grid"], G_COLS * G_ROWS))
        box = [(p.w("dd_ox", i), p.w("dd_oy", i)) for i in range(5)]
        w, h, d = screen(m)
        per = len(d) // (w * h)
        took += 1
        for r in range(G_ROWS):
            for c in range(G_COLS):
                if grid[r * G_COLS + c] not in (TT_WALL, TT_DOOR):
                    continue
                x0, y0 = c * tw, r * th
                if any(bx < x0 + tw and x0 < bx + tw and
                       by < y0 + th and y0 < by + th for bx, by in box):
                    continue            # somebody is standing on it
                cs = set()
                for y in range(y0, y0 + th):
                    if bdy + y >= h:
                        break
                    for x in range(x0, x0 + tw):
                        if not (pic[y * sb + (x >> 3)] >> (7 - (x & 7))) & 1:
                            continue
                        o = ((bdy + y) * w + (bdx + x)) * per
                        px = tuple(d[o:o + per])
                        if any(px):
                            cs.add(px)
                if not cs:
                    continue
                seen += 1
                if not any(q[2] > q[0] and q[2] > q[1] for q in cs):
                    wrong[(c, r)] = wrong.get((c, r), 0) + 1
    m.breakpoints([])
    m.go()
    stuck = sorted(((n, t) for t, n in wrong.items()), reverse=True)[:6]
    if took < frames // 2 or not seen:
        fail.append("%s: only %d finished frames and %d wall readings - this "
                    "leg read nothing" % (tag, took, seen))
    elif stuck and stuck[0][0] > cap:
        fail.append("%s: wall tile %s is in the actor's pen in %d of %d "
                    "finished frames (cap %d) - it is not being put back "
                    "(SPEC.md 93.5.13); the rest: %s"
                    % (tag, stuck[0][1], stuck[0][0], took, cap, stuck[1:]))
    else:
        say("%s: %d wall readings over %d finished frames, %d wrong, worst "
            "tile %d frame(s)" % (tag, seen, took, sum(wrong.values()),
                                  stuck[0][0] if stuck else 0))
    return fail


def settle_playing(m, p, secs=20.0):
    """Wait until the game is PLAYING, keeping Smiles in lives while we do.
    `secs` is an idle-box figure, spent as GUEST time."""
    c0 = m.status()["cycles"]
    while ((m.status()["cycles"] - c0) / os88marty.GUEST_HZ
           < secs * os88marty.GUEST_BUDGET_RATIO):
        m.pause()
        st = p.b("dd_state")
        m.write((p.seg << 4) + p.names["dd_lives"], bytes([99]))
        m.go()
        if st == 2:                     # DDS_PLAY
            return True
        if st == 0:                     # a game that ended: start another
            m.key("Enter")
        os88marty.pace(m, 0.3)
    return False


def leg_i(tag, ui, p, say, floor=0.15):
    """THE CAST IS ON THE GLASS.  Lit pixels inside each actor's own box.

    Eight legs of this row watched the dots, the maze, the title, the tile and
    the frame rate, and NOT ONE of them looked at Smiles or a ghost - so a
    refactor that drew every actor at the wrong position, clipped it to
    nothing and left the whole cast invisible passed all of them, on all
    three adapters, and reached the field.  This is that hole.

    The check is deliberately crude, because the defect it exists for is
    crude: a sprite fills a good share of its own tile and a corridor tile is
    black with at most a dot in it.  Fifteen readings, five actors on each of
    the three adapters, on the build that fixed it against the one that did
    not:

        drawing : 46 49 49 52 57 59 59 59 60 65 66 69 70 76 82   (min 46%)
        not     :  0  0  0  0  0  0  0  0  0  0  4  4 22 28 41

    The floor is **15%** - three times under the lowest a drawn actor has
    read, and clear of the 0-4% an undrawn one leaves.  It is deliberately not
    put in the 41-46 gap: those two ends are one sample each and the high
    "not" readings are not noise but a real thing - an actor standing inside
    somebody ELSE's band is drawn by it, which is SPEC.md 93.5.1 working.  A
    build with this defect still fails, because the other four read zero.

    GS_EYES is skipped: a pair of eyes is a fraction of the sprite it comes
    from, so it is the one state where a low reading is correct.

    BREAK IT ON PURPOSE: hoist dd_band_actor's dd_sx/dd_sy stores above its
    `call dd_band_build`.  dd_band_others walks the other four through that
    same pair, so the self actor is then composed at the last of them and
    clipped away.
    """
    m = ui.m
    fail = []
    if not settle_playing(m, p):
        say("%s: not playing after 20s - leg I skipped its sample" % tag)
        return fail
    m.pause()
    tw, th = p.w("dd_tw"), p.w("dd_th")
    bdx, bdy = p.w("dd_bdx"), p.w("dd_bdy")
    boxes = [(p.w("dd_x", i) >> 4, p.w("dd_y", i) >> 4, p.b("dd_alive", i),
              p.b("dd_gs", i) if i else 0) for i in range(5)]
    w, h, d = screen(m)
    m.go()
    per = len(d) // (w * h)
    seen = []
    for i, (x, y, alive, gs) in enumerate(boxes):
        if not alive or gs == 4:        # GS_EYES: a fraction of a sprite
            continue
        lit = 0
        for yy in range(y, y + th):
            if bdy + yy >= h:
                break
            for xx in range(x, x + tw):
                if bdx + xx >= w:
                    break
                o = ((bdy + yy) * w + (bdx + xx)) * per
                if any(d[o:o + per]):
                    lit += 1
        seen.append((i, lit / float(tw * th)))
    dark = [(i, f) for i, f in seen if f < floor]
    if not seen:
        fail.append("%s: no actor is alive - leg I read nothing" % tag)
    elif dark:
        fail.append("%s: actor(s) %s have under %.0f%% of their own box lit "
                    "(%s) - they are not being drawn (SPEC.md 93.5.1)"
                    % (tag, [i for i, _ in dark], 100 * floor,
                       ["%d:%.0f%%" % (i, 100 * f) for i, f in dark]))
    else:
        say("%s: all %d actors on the glass (%s of their own box lit)"
            % (tag, len(seen), ", ".join("%.0f%%" % (100 * f) for _, f in seen)))
    return fail


def run_arm(tag, machine, want_tile, a, say, floor=FPS_FLOOR):
    fail = []
    names = bss()
    with os88ui.boot(a.image, apps=a.apps, machine=machine) as ui:
        m = ui.m
        ui.path(PKG)
        p = Probe(ui, names)
        # dd_frames counts at the head of every draw, so a second one means
        # the first - the whole attract screen - has finished
        os88marty.until(m, lambda _: p.w("dd_frames") >= 2,
                        "the first whole frame", poll=0.2, limit=30)

        # --- D: the tile is cut from the surface ---------------------------
        tile = (p.w("dd_tw"), p.w("dd_th"))
        if not p.b("dd_ok"):
            fail.append("%s: the layout refused the content box (%dx%d)"
                        % (tag, p.w("dd_cw"), p.w("dd_ch")))
        if tile != want_tile:
            fail.append("%s: tile %dx%d, want %dx%d for a windowed %s "
                        "(SPEC.md 93.3's table)"
                        % (tag, tile[0], tile[1], want_tile[0], want_tile[1],
                           tag.upper()))
        else:
            say("%s: tile %dx%d, board %dx%d, %d bpp"
                % (tag, tile[0], tile[1], p.w("dd_mw"), p.w("dd_mh"),
                   p.b("dd_bpp")))

        # --- K: across and down are the same speed ON THE GLASS ------------
        # (SPEC.md 93.7.6).  A tile is not square on any adapter, so one tile
        # in DD_TILET ticks EITHER WAY makes an actor 15-30% faster along its
        # longer axis - which the field read straight off the screen.  The
        # claim is PHYSICAL: stpy converted by the same [dd_asp] the code
        # used has to match stpx.
        #
        # PER SPEED, because the failure this caught was ROUNDING and it only
        # shows on the adapter with the smallest steps: a CGA's frightened
        # ghost moves seven sixteenths of a pixel a tick, so one unit is 13%
        # of it, and converting a base speed once and taking 60% of THAT put
        # the two axes 13% apart on that row alone.
        asp = p.w("dd_asp")
        skewed = 0
        for i, sname in enumerate(("Smiles", "ghost", "frightened", "eyes",
                                   "tunnel")):
            sx, sy = p.w("dd_stpx", i), p.w("dd_stpy", i)
            phys = sy * asp / 100.0
            if phys <= 0:
                fail.append("%s: the %s speed is ZERO down - a frozen actor "
                            "(SPEC.md 93.7.1's rounding rule)" % (tag, sname))
                skewed += 1
                continue
            r = sx / phys
            if not 0.94 <= r <= 1.06:
                fail.append("%s: %s travels %.2fx as far ACROSS as DOWN on "
                            "the glass (%d vs %d x %d/100 = %.1f sixteenths a "
                            "tick) - the board is skewed and so is the "
                            "movement (SPEC.md 93.7.6)"
                            % (tag, sname, r, sx, sy, asp, phys))
                skewed += 1
        if not skewed:
            say("%s: across = down on the glass for all five speeds "
                "(aspect %d)" % (tag, asp))

        # --- A: the title screen has all four of its parts -----------------
        if p.b("dd_state") != 0:
            fail.append("%s: did not open on the attract screen (state %d)"
                        % (tag, p.b("dd_state")))
        if p.w("dd_titw") < 8 * 12:
            fail.append("%s: the title is %d px wide - it is twelve letters"
                        % (tag, p.w("dd_titw")))
        if p.w("dd_hs") == 0:
            fail.append("%s: the score table's first row is zero - the "
                        "built-in table never loaded" % tag)
        # ...and the demo is MOVING. Over GUEST ticks and not host seconds:
        # `time.sleep(2)` is 2 seconds of somebody else's box, and a lane
        # sharing four cores with two others hands the guest a third of the
        # work (docs/plans/SOAK-PARALLEL.md 1). This read `dd_x`/`dd_y` twice
        # 2 host seconds apart and reported "the attract screen is a still
        # picture" for a demo that was walking about perfectly well - a
        # 90-second watch of the same guest found no stall longer than one
        # sample.
        moved = (p.w("dd_x"), p.w("dd_y"))
        t0 = p.w("dd_anim")
        try:
            os88marty.until(m, lambda _: (p.w("dd_anim") - t0) & 0xFFFF >= 24,
                            "24 ticks of the demo", poll=0.1, limit=30)
        except os88marty.MartyError:
            pass
        ticks = (p.w("dd_anim") - t0) & 0xFFFF
        if ticks < 24:
            fail.append("%s: the game's own clock advanced %d ticks in 90 "
                        "guest seconds - the game is not running, so nothing "
                        "below this can be believed" % (tag, ticks))
        elif (p.w("dd_x"), p.w("dd_y")) == moved:
            fail.append("%s: the demo's Smiles has not moved in %d ticks of "
                        "the game's own clock - the attract screen is a still "
                        "picture" % (tag, ticks))

        # --- F: dots land in their own tiles, and the title is a title ------
        fail += leg_f(tag, ui, p, say)

        # --- B: the play line blinks ----------------------------------------
        seen = set()
        for _ in range(14):
            seen.add(screen(m)[2])
            if len(seen) >= 2:          # alive is all this asks
                break
            os88marty.pace(m, 0.35)
        if len(seen) < 2:
            fail.append("%s: the screen never changed over five seconds - "
                        "nothing on the attract screen is alive" % tag)

        # --- C: Enter starts a game and Smiles eats -------------------------
        m.key("Enter")
        # Until READY is over - the state that ends the wait, where this
        # was a blind 13.5 guest seconds. A game that never starts times
        # out here and the state test below says so.
        try:
            os88marty.until(m, lambda _: p.b("dd_state") not in (0, 1),
                            "READY to end", poll=0.1, limit=15)
        except os88marty.MartyError:
            pass
        # READY, PLAY, DIE or the flash between boards - anything but the
        # title. NOT `== PLAY`: nobody is steering Smiles for these three
        # seconds and since SPEC.md 93.8's ghosts hunt by line of sight one of
        # them catches him inside the window on the faster adapters, so an
        # exact state here failed for the AI working.
        if p.b("dd_state") not in (1, 2, 3, 4):
            fail.append("%s: Enter did not start a game (state %d)"
                        % (tag, p.b("dd_state")))
        dots0, score0 = p.w("dd_ndots"), p.w("dd_score")
        # STEER HIM, AND HOLD THE KEY. dd_input asks OSAPI_KEY_DOWN whether a
        # key IS DOWN once a logic step (SPEC.md 93.7.2), so a make-and-break
        # inside one frame is a keystroke the game is entitled to miss - which
        # is the whole point of polling rather than eventing. Left is where he
        # starts facing and a wall is where that ends, so a game nobody drives
        # eats a handful of dots and then stands still for ever.
        # Each hold ends as soon as he has eaten - the assertion is only
        # that he does - and is at most what it always was.
        ate = lambda: (p.w("dd_ndots") < dots0
                       and p.w("dd_score") > score0)
        for k in ("ArrowUp", "ArrowLeft", "ArrowDown", "ArrowRight",
                  "ArrowUp", "ArrowRight"):
            m.key(k, down=True, up=False)
            try:
                os88marty.until(m, lambda _: ate(), "Smiles to eat",
                                poll=0.1, guest=1.2 * os88marty.GUEST_PACE)
            except os88marty.MartyError:
                pass
            m.key(k, down=False, up=True)
            if ate():
                break
        dots1, score1 = p.w("dd_ndots"), p.w("dd_score")
        if dots1 >= dots0 or score1 <= score0:
            fail.append("%s: six seconds of steered play ate %d dots and "
                        "scored %d - Smiles is not eating (SPEC.md 93.9)"
                        % (tag, dots0 - dots1, score1 - score0))
        else:
            say("%s: ate %d dots for %d points"
                % (tag, dots0 - dots1, score1 - score0))

        # --- E: the frame is the tick ---------------------------------------
        for what in ("windowed", "fullscreen"):
            if what == "fullscreen":
                m.key("KeyF")
                # the bracket re-cuts the tile and then repaints the lot
                try:
                    os88marty.until(m, lambda _: (p.w("dd_tw"), p.w("dd_th"))
                                    != tile and p.b("dd_full") == 0,
                                    "the bracket's board", poll=0.2, limit=10)
                except os88marty.MartyError:
                    pass                # ...and the line below says so
                big = (p.w("dd_tw"), p.w("dd_th"))
                # STRICTLY BIGGER, and it holds again because a bracket is
                # always FULL (SPEC.md 93.3.3.1): 16x11 on a Hercules against
                # the window's Thin 8x9, 16x15 on a VGA, 16x6 on a CGA.  It
                # was briefly >= while Thin reached the bracket too, which
                # made fullscreen buy nothing.
                if big[0] * big[1] <= tile[0] * tile[1]:
                    fail.append("%s: the bracket's tile is %dx%d against the "
                                "window's %dx%d - fullscreen did not re-cut "
                                "the board from its own surface (SPEC.md "
                                "93.4.2)" % (tag, big[0], big[1],
                                             tile[0], tile[1]))
            # A WHOLE REPAINT IS NOT A FRAME (SPEC.md 93.5.3.1). It is 677 ms
            # on a VGA - twelve ticks - so a window that contains one loses
            # steps to DD_MAXSTEP and reads as a game that cannot keep up:
            # `ticks/s` falls to ~17.2 from 18.2, which is the tell. Five runs
            # of ONE build read 82.7 / 90.8 / 94.6 / 94.7 / 95.9 that way,
            # while an A/B of the change under suspicion - the same build and
            # scene, a `ret` poked over the routine and back - put its real
            # cost at 0.4 to 1.7 points. So the window is TAKEN AGAIN when a
            # repaint fell in it, rather than the row reporting the repaint as
            # a frame rate.
            for attempt in range(4):
                c0, t0, f0 = (m.status()["cycles"], p.w("dd_anim"),
                              p.w("dd_frames"))
                u0 = p.w("dd_fulls")
                # A RATE, and its span is read in cycles. 10 guest seconds
                # is ~182 ticks, so a frame either way is half a point -
                # it was 36, which only made a repaint likelier to land in it
                os88marty.guest_sleep(m, 10.0)
                c1, t1, f1 = (m.status()["cycles"], p.w("dd_anim"),
                              p.w("dd_frames"))
                if p.w("dd_fulls") == u0:
                    break
                say("%s %s: a whole repaint fell in the window - taking it "
                    "again" % (tag, what))
            gs = (c1 - c0) / CPU_HZ
            ticks, frames = (t1 - t0) / gs, (f1 - f0) / gs
            share = frames / ticks if ticks else 0.0
            say("%s %s: %.2f frames/s against %.2f ticks/s = %.1f%%"
                % (tag, what, frames, ticks, 100.0 * share))
            if share < floor:
                fail.append("%s %s: %.2f fps against a %.2f/s tick is %.1f%%, "
                            "under this arm's %.0f%% floor - the frame no "
                            "longer fits the tick (SPEC.md 93.6, and 93.5.3 "
                            "for the three things that have cost this before)"
                            % (tag, what, frames, ticks, 100.0 * share,
                               100.0 * floor))
        m.key("Escape")
        try:
            os88marty.until(m, lambda _: (p.w("dd_tw"), p.w("dd_th")) == tile
                            and p.b("dd_full") == 0,
                            "the window's board back", poll=0.2, limit=10)
        except os88marty.MartyError:
            pass                        # ...and the line below says so
        back = (p.w("dd_tw"), p.w("dd_th"))
        if back != tile:
            fail.append("%s: leaving the bracket left the tile at %dx%d, not "
                        "the window's %dx%d" % (tag, back[0], back[1],
                                                tile[0], tile[1]))

        # --- G: the maze is all still there ---------------------------------
        fail += leg_g(tag, ui, p, say)

        # --- H: ...and no band is about to take a piece of it ---------------
        if tag == "vga":
            fail += leg_h(tag, ui, p, say)

        # --- I: ...and the cast is actually on it ---------------------------
        fail += leg_i(tag, ui, p, say)
    return fail


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--arm", default=None,
                    help="one of vga, cga, herc (default: all three)")
    a = ap.parse_args(argv)
    say = lambda s: print("  " + s)

    arms = [x for x in ARMS if a.arm in (None, x[0])]
    if not arms:
        sys.exit("dotdel: no such arm %r - one of %s"
                 % (a.arm, ", ".join(x[0] for x in ARMS)))
    fail = []
    for tag, machine, tile, floor in arms:
        fail += run_arm(tag, machine, tile, a, say, floor)
    if fail:
        print("dotdel: %d FAILED" % len(fail))
        for f in fail:
            print("    FAIL: %s" % f)
        return 1
    print("dotdel: %d arm(s) passed" % len(arms))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
