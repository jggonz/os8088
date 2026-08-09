#!/usr/bin/env python3
"""The RAISE CACHE gate (SPEC.md 11.96) - is it taken, and does it put back
the right pixels?

    python3 tools/sucheck.py [machine]

Until this existed there was no test of 11.96 at all, and the reason is worth
knowing before you write another one: the cache is only taken for a window
that opted in with WF_SAVEU, and for a long time NOTE PAD was the only window
that did - so every scripted session that covered a Disk window, a Tracker or
a Task Manager and looked at the claim map saw nothing, and concluded the
feature was dead. It was not; nothing being driven had made the promise.

So this drives SOLITAIRE, which promises since SPEC.md 11.96.1, and asserts
the three things that can go wrong:

  * the cover REACHES wm_su_take at all - see the paragraph below, because
    the first version of this file did not and said PASS anyway;
  * the cache is CLAIMED, at the trivial purgeable rank (SPEC.md 50.6.4) -
    a claim at any other rank means the tag is wrong, and a tag that is wrong
    makes mem_pg_forget zero the wrong kernel word on a shed;
  * a cover/raise round trip reproduces the window - compared against the
    same window as W_PAINT drew it, which is the only reference that means
    anything.

The residual difference is the mouse arrow and the menu bar's application
name, both of which legitimately move between the two captures; it lands
around 80 bytes of 128,000 on CGA. A restore that is actually broken smears,
and smears in the hundreds or thousands.

THE COVER CLICK IS COMPUTED, NOT WRITTEN DOWN, and that is the whole lesson of
this file's first version. It clicked a fixed (300, 40) at the Disk window's
title bar - a point Solitaire's own rect contains, so the click went to the
window that was already frontmost, nothing was raised, nothing was covered and
wm_su_take was never entered. The pixel comparison then compared the screen
with itself and reported 78 differing bytes, which is exactly what a healthy
run reports. A gate whose passing figure is indistinguishable from its vacuous
one is worse than no gate. So the rects are read out of wm_wins and the point
is picked from the strip of the Disk window's title bar that Solitaire does
NOT cover, with an assertion that such a strip exists.

Raise it through the DOCK TILE rather than by clicking the window: after the
Disk window covers it, Solitaire may have no visible pixel left to click, and
a click that lands on bare desktop switches to Locator and raises nothing -
which reads exactly like a cache that did not restore.
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
W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
WF_USED, WF_VIS, WF_SAVEU = 1, 2, 32
TITLE_H = 18

DRIVE_B = (600, 108)        # the B: desktop zone on a 2-row CGA desktop
ROW_GAMES = (175, 84)       # B: root, sorted: APPS GAMES MEDIA SYSTEM
ROW_SOLIT = (175, 132)      # GAMES/, sorted: .. ARKANOID MINES MISSILE SOLITAIR
DOCK_TILE = (40, 190)       # ...and Solitaire's dock tile


def caches(m):
    """Every live PURGEABLE claim, by rank (SPEC.md 50.6.4)."""
    raw = m.read(m.sym("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        seg, para, own, _ = struct.unpack_from("<HHHH", raw, i * MC_SIZE)
        if seg and 0xFB <= (own >> 8) <= 0xFE:
            out.append((own, RANK[own >> 8], para / 64.0))
    return out


def windows(m):
    """Every live window record: (index, flags, x, y, w, h)."""
    raw = m.read(m.sym("wm_wins"), MAX_WIN * WIN_SIZE)
    out = []
    for i in range(MAX_WIN):
        b = i * WIN_SIZE
        f = struct.unpack_from("<H", raw, b + W_FLAGS)[0]
        if f & WF_USED:
            out.append((i, f) + struct.unpack_from("<HHHH", raw, b + W_X))
    return out


def cover_point(m):
    """A point on the un-promising window's title bar that the promising one
    does not cover. Clicking it raises that window OVER Solitaire, which is
    the only thing that reaches wm_su_take."""
    wins = [w for w in windows(m) if w[1] & WF_VIS]
    over = [w for w in wins if w[1] & WF_SAVEU]
    under = [w for w in wins if not w[1] & WF_SAVEU]
    if len(over) != 1 or len(under) != 1:
        raise SystemExit("sucheck: want exactly one WF_SAVEU window and one "
                         "other; got %r" % (wins,))
    _, _, sx, sy, sw, sh = over[0]
    _, _, dx, dy, dw, dh = under[0]
    y = dy + TITLE_H // 2
    if not (sy <= y < sy + sh):
        return (dx + dw // 2, y)        # Solitaire is not on this row at all
    left, right = dx + 24, sx - 4       # clear of the close box, clear of it
    if right - left < 8:
        left, right = sx + sw + 4, dx + dw - 24     # ...or the strip is right
    if right - left < 8:
        raise SystemExit("sucheck: no clickable strip of the Disk window's "
                         "title bar is uncovered (disk=%r solitaire=%r)"
                         % (under[0][2:], over[0][2:]))
    return ((left + right) // 2, y)


def fb(m):
    return b"".join(bytes(r) for r in m.vram("cga")[2])


def wait_desktop(m, limit=180):
    """settle()'s menu-bar gate fires on the SPLASH on a slow machine or a
    full apps disk, so wait on the desktop's own lit count instead."""
    t0 = time.time()
    while time.time() - t0 < limit:
        if sum(sum(r) for r in m.vram("cga")[2]) > 70000:
            return
        time.sleep(2)
    raise SystemExit("sucheck: no desktop after %ds" % limit)


def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga"
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine, boot=2) as m:
        wait_desktop(m)
        mo = Mouse(marty=m)
        mo.dblclick(*DRIVE_B);   time.sleep(3)
        mo.dblclick(*ROW_GAMES); time.sleep(3)
        mo.dblclick(*ROW_SOLIT); time.sleep(12)
        painted = fb(m)
        disk_title = cover_point(m)
        print("solitaire up      %s" % (caches(m) or "no caches"))
        print("cover click at    %r  (windows %r)"
              % (disk_title, [w[:1] + w[2:] for w in windows(m)]))

        mo.click(*disk_title); time.sleep(3)        # cover it: this is the take
        covered = caches(m)
        print("covered           %s" % (covered or "no caches"))

        mo.click(*DOCK_TILE); time.sleep(4)         # ...and the restore
        restored = fb(m)
        print("raised (dock)     %s" % (caches(m) or "no caches"))

        diff = sum(1 for a, b in zip(painted, restored) if a != b)
        print()
        print("W_PAINT original vs raise: %d differing bytes of %d"
              % (diff, len(painted)))
        took = [c for c in covered if c[0] >> 8 == 0xFB]
        bad = []
        if not took:
            bad.append("no trivial-rank claim was live while the window was "
                       "covered - the cache was not taken")
        if diff > 400:
            bad.append("the restore does not reproduce W_PAINT's pixels")
        for c in took:
            print("cache taken at 0x%04X (%s), %.0fKB" % c)
        m.quit()
    if bad:
        raise SystemExit("FAIL: " + "; ".join(bad))
    print("PASS")


if __name__ == "__main__":
    main()
