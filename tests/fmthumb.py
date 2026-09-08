#!/usr/bin/env python3
"""SPEC.md 13.10.5: the Disk window's scroll-bar THUMB is dragged.

    python3 tests/fmthumb.py [machine]

A registered soak row: SPEC.md 13.10.5's gesture SHIPS now, so this needs no
knob and no defines. `--rate=n` matches a `make SBRATE=n` build.

Five cases, and the middle three are the request:

  A  a press on the thumb no longer PAGES     (FS_SCRL is unchanged by it)
  B  ...the thumb MOVES with the hand         (pixels, in the track)
  C  ...and the VIEW does not, at rate 0      (FS_SCRL still unchanged)
  D  x is never read (13.10.5.2): the same drag with the pointer 150px off
     the bar lands on the SAME pos
  E  the release commits                      (FS_SCRL = the dragged pos)

The signal for A, C and E is FS_SCRL - one word of kernel memory rather than
a picture - which is tests/mouseup.py's discipline: a screenshot of a list
that scrolled and a list that did not differ by rows of text, and a count of
lit pixels cannot tell one from a repaint that happened to reflow.

B is the one case that HAS to be pixels, because the whole claim is that the
thumb is somewhere the state block does not say it is.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, "tools")
import os88build as _B
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
MACHINE = ARGS[0] if ARGS else "os8088_5150_cga_gla"
# THE BUILD'S OWN RATE (13.10.5.4), because case C's expectation INVERTS on it:
# at 0 the view must not move mid-drag and at n>0 it must. A gate that only
# knew the rate-0 answer would pass a build that had stopped tracking at all.
RATE = int(next((a.split("=")[1] for a in sys.argv[1:]
                 if a.startswith("--rate=")), "0"))
SHOT = next((a.split("=")[1] for a in sys.argv[1:]
             if a.startswith("--shot=")), None)
W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
SBCELL, SBMINH = 10, 8

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def disk_win(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_H) > 60:
            return i, tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None, None


def ksb(m):
    """os88ui_ksb, the seven words the last painter filled."""
    b = m.read(m.sym("os88ui_ksb"), 14)
    return [_u16(b, i * 2) for i in range(7)]


def thumb(blk):
    """os88ui_sbthumb, in Python. None = there is no thumb."""
    x1, y1, x2, y2, total, fit, pos = blk
    if total <= fit:
        return None
    top = y1 + SBCELL + 1
    th = (y2 - SBCELL - 1) - top + 1
    if th < SBMINH:
        return None
    h = max((fit * th) // total, SBMINH)
    t = top + (pos * th) // total
    return min(t, top + th - h), h


def scrl(m):
    vp = int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
    return _u16(m.read((M.KERNEL_SEG << 4) + vp + FS_SCRL_OFS, 2), 0)


def dragpos(m):
    return int.from_bytes(m.read(m.sym("os88ui_sbd_pos"), 2), "little")


def dragon(m):
    return m.read(m.sym("os88ui_sbd_on"), 1)[0]


def lit(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    n = 0
    for y in range(rect[1], rect[3] + 1):
        for x in range(rect[0], rect[2] + 1):
            i = (y * w + x) * 3
            if px[i] or px[i + 1] or px[i + 2]:
                n += 1
    return n


def sig(m, mono, rect):
    """The rect's CONTENT, not its lit count - a thumb that relocates inside
    the track keeps roughly the same number of lit pixels, so a count cannot
    see the move and a byte-for-byte signature can."""
    if mono:
        w, h, rows = m.vram()
        return tuple(tuple(rows[y][rect[0]:rect[2] + 1])
                     for y in range(rect[1], rect[3] + 1))
    w, h, px = m.fbuf()
    return tuple(bytes(px[(y * w + rect[0]) * 3:(y * w + rect[2] + 1) * 3])
                 for y in range(rect[1], rect[3] + 1))


src = open("kernel/files.inc").read()
FS_SCRL_OFS = int(re.search(r"^FS_SCRL\s+equ\s+(\d+)", src, re.M).group(1))

# A B: DISK OF ITS OWN, and 30 files rather than the apps disk's six. The
# quantisation is real (13.10.5.3): with `total` 6 and `fit` 5 the whole track
# maps onto ONE row of travel, so a drag the length of it moves the view by a
# row and a drag most of the way moves it by nothing - which is a correct bar
# and a useless gate. This disk makes the thumb small and the range long.
DISK = _B.at("build/thumbdisk.img")
if not os.path.exists(DISK):
    os.makedirs("build/thumbsrc", exist_ok=True)
    names = []
    for i in range(30):
        n = "build/thumbsrc/FILE%02d.TXT" % i
        open(n, "w").write("row %d\n" % i)
        names.append(n)
    subprocess.check_call([sys.executable, "tools/os88disk.py",
                           "-o", DISK, "--size", "360"] + names)

with M.launch("build/os8088-360.img", apps=DISK,
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : the thumb drag (SPEC.md 13.10.5) ==")

    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, 80)                # zone 1 = B:, the 30-file disk
                                            # above (zone 0 is A:, whose root
                                            # lists six things and cannot make
                                            # a thumb worth dragging)
    M.settle(m)
    slot, w = disk_win(m)
    check("a Disk window opened", w is not None, f"{w}")
    if not w:
        sys.exit("no window")

    blk = ksb(m)
    t = thumb(blk)
    check("...with a scrollable listing", t is not None,
          f"(bar {blk[:4]}, total {blk[4]}, fit {blk[5]}, pos {blk[6]})")
    if not t:
        sys.exit("nothing to scroll - the root has too few entries")
    top, h = t
    x1, y1, x2, y2, total, fit, _ = blk
    cx = (x1 + x2) // 2
    track_top, track_h = y1 + SBCELL + 1, (y2 - SBCELL - 1) - (y1 + SBCELL) 
    print(f"  thumb: top {top} h {h}; track {track_top}..{y2-SBCELL-1}; "
          f"total {total} fit {fit}")

    # --- A: the press takes the thumb and scrolls nothing -----------------
    was = scrl(m)
    mo.to(cx, top + h // 2)
    mo._edge(True)
    time.sleep(0.8)
    check("a press on the thumb does not PAGE", scrl(m) == was,
          f"(FS_SCRL {scrl(m)}, was {was})")
    check("...and the gesture is live", dragon(m) == 1)

    # --- B/C: drag DOWN. The thumb moves; the view does not ---------------
    band = (x1 + 1, track_top, x2 - 1, y2 - SBCELL - 1)
    before = sig(m, mono, band)
    target = y2 - SBCELL - 1                 # the very BOTTOM row of the track:
    # aiming h//2 higher leaves the wanted top ONE pixel short of the travel
    # (pos 24 of 25 on the 30-file disk, measured), where the bottom row
    # clamps the wanted top AT the travel and the commit is exactly total-fit
    mo.to(cx, target, l=True)
    time.sleep(1.2)
    after_pos = dragpos(m)
    blk2 = ksb(m)
    t2 = thumb(blk2)
    if SHOT:
        if mono:
            wd, ht, rows = m.vram()
            M.write_png(SHOT, wd, ht, rows)
        else:
            wd, ht, px = m.fbuf()
            M.write_png_rgb(SHOT, wd, ht, px)
        print(f"  mid-drag frame -> {SHOT}")
    check("the thumb moved with the hand",
          t2 is not None and t2[0] > top + 2,
          f"(top {top} -> {t2 and t2[0]}, block pos {blk2[6]})")
    check("...the ELEMENT tracked it", after_pos > 0, f"(pos {after_pos})")
    check("...and the GLASS moved with it", sig(m, mono, band) != before,
          "(the track band's pixels are byte-identical to the pre-drag frame)")
    if RATE == 0:
        check("...and the VIEW did not, at rate 0", scrl(m) == was,
              f"(FS_SCRL {scrl(m)}, was {was})")
    else:
        check(f"...and the VIEW followed, at rate {RATE}", scrl(m) != was,
              f"(FS_SCRL {scrl(m)}, was {was}, hand at {after_pos})")

    # --- D: x is never read ------------------------------------------------
    mo.to(cx - 150, target, l=True)
    time.sleep(1.2)
    check("x is never read: 150px off the bar is the same pos",
          dragpos(m) == after_pos, f"({dragpos(m)} vs {after_pos})")
    mo.to(cx + 60, target, l=True)
    time.sleep(1.2)
    check("...on the other side too", dragpos(m) == after_pos,
          f"({dragpos(m)} vs {after_pos})")

    # --- E: the release commits -------------------------------------------
    want = dragpos(m)
    mo._edge(False)
    M.settle(m)
    check("the release commits the dragged pos", scrl(m) == want,
          f"(FS_SCRL {scrl(m)}, wanted {want})")
    check("...and the gesture is over", dragon(m) == 0)
    check("...the bar's thumb agrees with the view",
          ksb(m)[6] == want, f"(block pos {ksb(m)[6]})")
    # The hand has sat at the BOTTOM of the track since case B, so the
    # committed pos must be the LAST page - not the element's own answer
    # echoed back (which is what the check above is), but the absolute end.
    # pos and top map end onto end (13.10.5.3), and the min-height clamp is
    # exactly where the old pos*track/total form lost that: a clamped thumb
    # dragged to the bottom left the view rows short of the end.
    check("...and the track's END is the LAST page", want == total - fit,
          f"(pos {want}, total {total} fit {fit})")


print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
