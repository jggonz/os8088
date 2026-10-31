#!/usr/bin/env python3
"""win16sol - Solitaire, Windows 3.0's and 3.1's, on all four adapters
(SPEC.md 124.20, docs/plans/WIN16-ANY-PLAN.md S15).

    python3 tests/win16sol.py                 every adapter (row win16sol)
    python3 tests/win16sol.py --video cga     one of vga, ega, cga, herc
    python3 tests/win16sol.py --keep          leave the last QEMU for a look

*As a user, I double-click Solitaire and play it.*

SOL.EXE is 180 KB: the first program here over 64 KB, and the reason S15
reads a file in pieces (124.20.1). W31SOL.EXE is Windows 3.1's, copied there
by the win16-reference skill under that name. Microsoft's files are never in
this tree, so without them the row is a SKIP.

One boot per adapter: VGA is the shipped kernel, EGA and CGA private
`VIDEO=` kernels, and Hercules `VIDEO=herc HERCSEG=0x7000`, read back out of
RAM because QEMU has no Hercules card (docs/HERCULES-TESTING.md). On each,
each Solitaire in turn, the other closed first (two at once are short of
memory, and 3.1 says so and drags outlines - correctly):

  1. it opens a window titled Solitaire and WIN16 says Running;
  2. it has DEALT: the client is not one colour. The felt is the client's
     commonest colour on every adapter (green, or lit on a 1bpp display), and
     at least a tenth of the client is something else - seven piles and the
     deck are about a third;
  3. a card MOVES: a click on the deck turns cards onto the waste pile beside
     it, and that region changes by at least 1,500 pixels;
  4. its close box ends it.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, b, u16                         # noqa: E402
import win16fpu as F                                          # noqa: E402

W16S_REFUSED, W16S_RUNNING, W16S_ENDED = 2, 3, 4
PROGS = ("SOL.EXE", "W31SOL.EXE")
VIDEOS = ("vga", "ega", "cga", "herc")
HERCSEG = 0x7000


def progs():
    have = [p for p in PROGS if os.path.exists(b(os.path.join("win16", p)))]
    for p in PROGS:
        if p not in have:
            print("  (no build/win16/%s: skipped)" % p)
    return have


def system_for(video):
    """the system disk for an adapter, and the tree os88sym reads"""
    if video == "vga":
        return b("os8088.img")
    knobs = ("VIDEO=herc", "HERCSEG=0x%X" % HERCSEG) if video == "herc" \
        else ("VIDEO=" + video,)
    return os88build.tree(*knobs, targets=("os8088.img",)).apply().img(
        "os8088.img")


class SDrive(QDrive):
    """QDrive with the system disk of the adapter under test"""

    def __init__(self, work, sysimg, bimg, keep):
        import os88qemu
        import xmcheck
        self.q, self.x = os88qemu, xmcheck
        self.p = os88qemu.Private(work, sysimg, bimg)
        self.sock = self.p.sock
        if keep:
            import atexit
            atexit.unregister(self.p.close)
        else:
            os88qemu.own(os.path.join(work, "qemu.pid"), self.sock)
        xmcheck.wait_desktop(self.sock, "win16sol")


def glass(d, video, work):
    """the screen in GUEST pixels: rows of values, one per pixel (an RGB
    tuple, or 0/1 on Hercules)"""
    import shot
    vw = u16(d.read(d.x.sym("vid_w"), 2), 0)
    vh = u16(d.read(d.x.sym("vid_h"), 2), 0)
    if video == "herc":
        fb = d.read(HERCSEG << 4, 4 * 0x2000)
        rows = []
        for y in range(vh):
            base = (y % 4) * 0x2000 + (y // 4) * 90
            rows.append([(fb[base + (x >> 3)] >> (7 - (x & 7))) & 1
                         for x in range(vw)])
        return vw, vh, rows
    path = os.path.join(work, "s.ppm")
    d.hmp("screendump %s" % path)
    w, h, rgb = shot.read_ppm(path)
    rows = []
    for y in range(vh):
        sy = y * h // vh
        rows.append([tuple(rgb[3 * (sy * w + x * w // vw):3 * (sy * w + x * w // vw) + 3])
                     for x in range(vw)])
    return vw, vh, rows


def region(rows, x0, y0, x1, y1):
    return [r[max(x0, 0):x1] for r in rows[max(y0, 0):y1]]


def window_of(ui, p):
    for w in ui.windows():
        if w.i == p.w.i:
            return w
    return None


def run_one(d, ui, S, dw, name, video, work, bad):
    tag = "%s %s" % (video.upper(), name)
    from win16cpu import raise_
    raise_(d, ui, dw)
    p = F.Prog(d, ui, d.x.sym, S, name)
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or p.text("w16_l2") == "Running", 300, "%s to run" % name)
    d.idle(6.0)
    st = p.pb("w16_state")
    if st != W16S_RUNNING:
        bad.append("%s: WIN16 %s it: %s / %s" % (tag, "refused" if st == W16S_REFUSED
                   else "ended", p.text("w16_l1"), p.text("w16_l2")))
        return
    w = window_of(ui, p)
    title = w.title if w else ""
    print("%s: running, window %r" % (tag, title))
    if "Solitaire" not in title:
        bad.append("%s: the window is %r, not Solitaire" % (tag, title))
        return
    cx0, cy0, cx1, cy1 = w.content
    menu = 20                           # the bar, at most, on every adapter
    d.q.mouse_to(d, d.x.sym, d.hmp, cx1 - 4, cy0 + 2)   # off the cards
    d.idle(1.0)
    vw, vh, rows = glass(d, video, work)
    cy1 = min(cy1, vh - 1)
    client = region(rows, cx0, cy0 + menu, cx1, cy1 - 16)   # less the status
    flat = [v for r in client for v in r]
    if not flat:
        bad.append("%s: no client on the screen" % tag)
        return
    felt = max(set(flat), key=flat.count)
    other = sum(1 for v in flat if v != felt) / float(len(flat))
    print("   dealt: %.0f%% of the client is not the felt %r" % (100 * other, felt))
    if other < 0.10:
        bad.append("%s: %.0f%% of the client is not the felt - nothing dealt"
                   % (tag, 100 * other))
    # the deck is the top-left card: client (11-16, 7) to (82-90, 103)
    before = region(rows, cx0 + 90, cy0 + menu, cx0 + 280, cy0 + menu + 110)
    d.click(cx0 + 40, cy0 + menu + 50)
    d.idle(4.0)
    d.q.mouse_to(d, d.x.sym, d.hmp, cx1 - 4, cy0 + 2)
    d.idle(1.0)
    _, _, rows2 = glass(d, video, work)
    after = region(rows2, cx0 + 90, cy0 + menu, cx0 + 280, cy0 + menu + 110)
    changed = sum(1 for ra, rb in zip(before, after)
                  for va, vb in zip(ra, rb) if va != vb)
    print("   the deck clicked: %d pixels of the waste changed" % changed)
    if changed < 1500:
        bad.append("%s: a click on the deck changed %d pixels beside it - no "
                   "card turned" % (tag, changed))
    d.click(*geom.close_xy(w.x, w.y, w.w, m=d, sym=d.x.sym))
    d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
           "%s to end by its close box" % name)
    d.idle(4.0)


def leg(video, names, keep, bad):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16s-")     # short: a QMP socket path
    try:
        sysimg = system_for(video)
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, names)
        d = SDrive(work, sysimg, bimg, keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        for n in names:
            run_one(d, ui, S, dw, n, video, work, bad)
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", choices=VIDEOS)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    names = progs()
    if not names:
        print("win16sol: SKIP - neither SOL.EXE nor W31SOL.EXE is in build/win16 "
              "(the win16-reference skill)")
        return 77
    bad = []
    for v in ([a.video] if a.video else VIDEOS):
        leg(v, names, a.keep, bad)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16sol: %s dealt and played on %s" % (" and ".join(names),
              ", ".join(v.upper() for v in ([a.video] if a.video else VIDEOS))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
