#!/usr/bin/env python3
"""The RAISE CACHE gate (SPEC.md 11.96) - is it taken, does it put back the
right pixels, and does EVERY promising window get one (11.96.3)?

    python3 tools/sucheck.py [machine]        # any adapter: see below

Until this existed there was no test of 11.96 at all, and the reason is worth
knowing before you write another one: the cache is only taken for a window
that opted in with WF_SAVEU, and for a long time NOTE PAD was the only window
that did - so every scripted session that covered a Disk window, a Tracker or
a Task Manager and looked at the claim map saw nothing, and concluded the
feature was dead. It was not; nothing being driven had made the promise.

So this drives SOLITAIRE and MINESWEEPER, which both promise since SPEC.md
11.96.1, in two phases:

  ONE WINDOW - the cache is CLAIMED, at the trivial purgeable rank (SPEC.md
    50.6.4; a claim at any other rank means the tag is wrong, and a tag that
    is wrong makes mem_pg_forget zero the wrong kernel word on a shed), and a
    cover/raise round trip reproduces the window, compared against the same
    window as W_PAINT drew it.

  TWO WINDOWS - both are covered before either is raised, so BOTH caches must
    be live at once. That is the whole of 11.96.3: with one cache for the
    machine the second take dropped the first, and the discriminating
    observation is two trivial-rank claims in mem_tab at the same moment.
    Then each is raised in turn and each window's own content rect is
    compared against its own reference.

The residual difference is the mouse arrow and the menu bar's application
name, both of which legitimately move between the two captures; the
full-screen figure lands around 120 bytes of 128,000 on CGA, and a per-window
crop that excludes the arrow lands at 0. A restore that is actually broken
smears, and smears in the hundreds or thousands.

THE COVER CLICK IS COMPUTED, NOT WRITTEN DOWN, and that is the whole lesson of
this file's first version. It clicked a fixed (300, 40) at the Disk window's
title bar - a point Solitaire's own rect contains, so the click went to the
window that was already frontmost, nothing was raised, nothing was covered and
wm_su_take was never entered. The pixel comparison then compared the screen
with itself and reported 78 differing bytes, which is BETTER than a healthy
run's 124. A gate whose vacuous figure beats its passing one is worse than no
gate. So the rects are read out of wm_wins and the point is picked from the
strip of the Disk window's title bar that nothing else covers, with an
assertion that such a strip exists.

Raise through the DOCK TILE rather than by clicking the window: once the Disk
window covers it, a promising window may have no visible pixel left to click,
and a click that lands on bare desktop switches to Locator and raises nothing
- which reads exactly like a cache that did not restore. The tile is found
through wm_owner (window slot -> instance slot) rather than counted off by
eye, because the Disk window is an instance too and holds tile 0.

IT WAITS WITH `launch`'s OWN GATE and has no boot poll of its own, which it
did have: `settle` used to gate on the menu bar's white field alone and read
the screen for that a round trip apart from reading it for stillness, so on
an emulator running the guest faster than real time the two could answer
about a state that never existed. It tests the whole desktop from one frame
now (os88marty `_desktop_up`), so `boot=True` is the wait again.

EVERY COORDINATE IS DERIVED, so this runs on all three adapters. The drive
zone is desk_ord_xy's arithmetic (SPEC.md 26.1) over [vid_desk_zx] and
[desk_rows] read out of the guest - 2 rows on CGA, 4 on Hercules, 7 on VGA,
so a written-down zone is a different drive on each; the file rows are
fm_layout's (FM_ROW_Y0 + i*FM_ROW_H below the content top). The frame itself
is the awkward one and `fb()` below carries the reasoning: the CARD's frame is
not in the GUEST's coordinate system, so 1bpp reads video memory and VGA -
which has none to read - asserts the card's frame is the size the guest thinks
the screen is rather than assuming it.
"""
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse

MEM_MAX, MC_SIZE = 32, 8
RANK = {0xFB: "trivial", 0xFC: "low", 0xFD: "medium", 0xFE: "high"}

MAX_WIN, WIN_SIZE = 12, 26
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE, W_SEG = 0, 2, 4, 6, 8, 10, 22
WF_USED, WF_VIS, WF_SAVEU = 1, 2, 32
TITLE_H, KERNEL_SEG = 18, 0x0060

DOCK_X0, DOCK_STEP, DOCK_TILE_W = 8, 28, 24     # kernel/dock.inc
DESK_ZW, DESK_ZH = 48, 44                       # kernel/desk.inc
DESK_ZY0, DESK_ZSTEP, DESK_COLW = 32, 60, 56
FM_ROW_Y0, FM_ROW_H = 22, 16                    # kernel/files.inc

VOL_B = 1                   # B:, the apps floppy
ROW_GAMES = 1               # B: root, sorted: APPS GAMES MEDIA SYSTEM
ROW_MINES = 2               # GAMES/, sorted: .. ARKANOID MINES MISSILE SOLITAIR
ROW_SOLIT = 4


class Win(object):
    def __init__(self, m, i, raw):
        b = i * WIN_SIZE
        self.i = i
        self.flags = struct.unpack_from("<H", raw, b + W_FLAGS)[0]
        self.x, self.y, self.w, self.h = struct.unpack_from("<HHHH", raw,
                                                            b + W_X)
        tp, seg = struct.unpack_from("<H", raw, b + W_TITLE)[0], \
            struct.unpack_from("<H", raw, b + W_SEG)[0]
        s = m.readseg(seg or KERNEL_SEG, tp, 24)
        self.title = bytes(s).split(b"\0")[0].decode("latin-1")

    @property
    def visible(self):
        return bool(self.flags & WF_VIS)

    @property
    def promises(self):
        return bool(self.flags & WF_SAVEU)

    @property
    def content(self):
        """The rect wm_su_rect answers, which is what the cache holds."""
        return (self.x + 1, self.y + TITLE_H, self.x + self.w - 2,
                self.y + self.h - 2)

    def covers(self, x, y):
        return (self.x <= x < self.x + self.w
                and self.y <= y < self.y + self.h)

    def __repr__(self):
        return "%s@%d(%d,%d,%d,%d)%s" % (self.title, self.i, self.x, self.y,
                                         self.w, self.h,
                                         "+saveu" if self.promises else "")


def windows(m):
    raw = m.read(m.sym("wm_wins"), MAX_WIN * WIN_SIZE)
    return [Win(m, i, raw) for i in range(MAX_WIN)
            if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & WF_USED]


def caches(m):
    """Every live PURGEABLE claim, by rank (SPEC.md 50.6.4)."""
    raw = m.read(m.sym("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        seg, para, own, _ = struct.unpack_from("<HHHH", raw, i * MC_SIZE)
        if seg and 0xFB <= (own >> 8) <= 0xFE:
            out.append((own, RANK[own >> 8], para / 64.0))
    return out


def trivial(m):
    return sorted(c for c in caches(m) if c[0] >> 8 == 0xFB)


def word(m, sym):
    return struct.unpack("<H", m.read(m.sym(sym), 2))[0]


def tile(m, win):
    """The dock tile of the instance owning a window (SPEC.md 30)."""
    inst = m.read(m.sym("wm_owner"), MAX_WIN)[win.i]
    if inst == 0xFF:
        raise SystemExit("sucheck: %r has no instance, so no dock tile" % win)
    return (DOCK_X0 + inst * DOCK_STEP + DOCK_TILE_W // 2,
            word(m, "vid_dock_y0") + 10)


def zone(m, ordinal):
    """desk_ord_xy's arithmetic (SPEC.md 26.1), centred - zones fill a column
    downwards and wrap to a new one on the LEFT, and how many fit is
    [desk_rows]: 2 on CGA, 4 on Hercules, 7 on VGA."""
    rows = word(m, "desk_rows")
    col, row = divmod(ordinal, rows)
    x = word(m, "vid_desk_zx") - col * DESK_COLW
    y = DESK_ZY0 + row * DESK_ZSTEP
    return (x + DESK_ZW // 2, y + DESK_ZH // 2)


def row(win, i):
    """fm_layout's list geometry: row i's middle, in the content."""
    return (win.x + 60, win.y + TITLE_H + FM_ROW_Y0 + i * FM_ROW_H
            + FM_ROW_H // 2)


def cover_point(m, target):
    """A point on `target`'s title bar that no other visible window covers.
    Clicking it raises target over everything, which is the only thing that
    reaches wm_su_take."""
    others = [w for w in windows(m) if w.visible and w.i != target.i]
    y = target.y + TITLE_H // 2
    # clear of the close box on the left and the minimize box on the right
    for x in range(target.x + 24, target.x + target.w - 24):
        if not any(o.covers(x, y) for o in others):
            return (x, y)
    raise SystemExit("sucheck: no uncovered strip of %r's title bar "
                     "(others %r)" % (target, others))


def fb(m):
    """The screen as (width, bytes-per-pixel, data), IN GUEST COORDINATES.

    THE CARD'S RASTERISED FRAME IS NOT IN THE GUEST'S COORDINATE SYSTEM, and
    that is why this is not simply `fbuf` everywhere. Measured against the
    guest's own framebuffer by correlation, MartyPC's Hercules `fbuf` is
    720x350 for a 720x348 screen and sits at dx = -16, dy = +2 - a perfect
    match at that alignment and nowhere else. A crop taken at guest
    coordinates therefore samples 16 columns to the right of what it names,
    and the failure is invisible in the middle of a window and appears only
    where the crop leaves the window at one edge and enters it at the other:
    it reported 1,670 differing pixels of Minesweeper, all of them in the
    rightmost 14 columns, and every one of them the window BEHIND it. That
    reads as a smeared restore, which is exactly the bug this gate is for.

    So on the 1bpp adapters the guest's own framebuffer is what is read -
    `vram` decodes SPEC.md 39.3's banked layout and is in guest coordinates
    by construction. VGA has no flat framebuffer to read (four planes behind
    the Graphics Controller), so it must use the card's frame, and the
    dimensions are ASSERTED against [vid_w]/[vid_h] rather than assumed: an
    offset frame there would be the same silent mis-crop, and refusing is the
    only honest answer this file can give without a second source to
    correlate against.
    """
    if m.read(m.sym("vid_mono"), 1)[0]:
        w, _, rows = m.vram()
        return (w, 1, b"".join(bytes(r) for r in rows))
    w, h, data = m.fbuf()
    gw, gh = word(m, "vid_w"), word(m, "vid_h")
    if (w, h) != (gw, gh):
        raise SystemExit("sucheck: the card's frame is %dx%d for a %dx%d "
                         "screen, so it is padded or offset and a crop at "
                         "guest coordinates would be wrong (see fb())"
                         % (w, h, gw, gh))
    return (w, 3, data)


def crop(frame, rect):
    w, bpp, data = frame
    x1, y1, x2, y2 = rect
    return b"".join(data[(y * w + x1) * bpp:(y * w + x2 + 1) * bpp]
                    for y in range(y1, y2 + 1))


def pixdiff(frame, a, b):
    """Differing PIXELS, not bytes - `fbuf` is rgb24 and `vram` is a byte."""
    bpp = frame[1]
    return sum(1 for i in range(0, min(len(a), len(b)), bpp)
               if a[i:i + bpp] != b[i:i + bpp])


def named(m, title):
    for w in windows(m):
        if w.title.upper().startswith(title):
            return w
    raise SystemExit("sucheck: no window titled %r (have %r)"
                     % (title, windows(m)))


def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga"
    bad = []
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        mo.dblclick(*zone(m, VOL_B)); time.sleep(4)
        disk = [w for w in windows(m) if w.visible][0]
        mo.dblclick(*row(disk, ROW_GAMES)); time.sleep(4)
        mo.dblclick(*row(disk, ROW_SOLIT)); time.sleep(14)

        sol = named(m, "SOLIT")
        disk = [w for w in windows(m) if not w.promises and w.visible][0]
        print("windows           %r" % (windows(m),))

        # ---- phase 1: one window, the whole screen ------------------------
        painted = fb(m)
        mo.click(*cover_point(m, disk)); time.sleep(3)   # cover: this is the
        one = trivial(m)                                 # take
        print("1: covered        %s" % (one or "NO TRIVIAL CLAIM"))
        mo.click(*tile(m, sol)); time.sleep(4)           # ...and the restore
        now = fb(m)
        d1 = pixdiff(now, painted[2], now[2])
        print("1: raised         %s   screen differs by %d of %d pixels"
              % (trivial(m) or "no caches", d1, len(now[2]) // now[1]))
        if not one:
            bad.append("phase 1: the cache was not taken")
        if d1 > 800:
            bad.append("phase 1: the restore does not reproduce W_PAINT "
                       "(%d pixels)" % d1)

        # ---- phase 2: two windows, both banked at once (SPEC.md 11.96.3) --
        ref = {sol.i: crop(fb(m), sol.content)}
        mo.click(*cover_point(m, disk)); time.sleep(3)      # bank Solitaire
        mo.dblclick(*row(disk, ROW_MINES)); time.sleep(12)  # ...and launch
        mines = named(m, "MINE")
        ref[mines.i] = crop(fb(m), mines.content)
        print("2: mines up       %r" % (windows(m),))
        mo.click(*cover_point(m, disk)); time.sleep(3)      # bank Minesweeper
        two = trivial(m)
        print("2: both covered   %s" % (two or "NO TRIVIAL CLAIM"))
        if len(two) < 2:
            bad.append("phase 2: %d trivial claim(s) live with two covered "
                       "promising windows - a second take is still dropping "
                       "the first (SPEC.md 11.96.3)" % len(two))
        for w in (sol, mines):
            mo.click(*tile(m, w)); time.sleep(4)
            now = [x for x in windows(m) if x.i == w.i][0]
            f = fb(m)
            d = pixdiff(f, ref[w.i], crop(f, now.content))
            print("2: %-15s content differs by %d pixels of %d"
                  % (now.title + " back", d, len(ref[w.i]) // f[1]))
            if d > 200:
                bad.append("phase 2: %s came back wrong (%d pixels)"
                           % (now.title, d))
        m.quit()
    print()
    if bad:
        raise SystemExit("FAIL: " + "; ".join(bad))
    print("PASS")


if __name__ == "__main__":
    main()
