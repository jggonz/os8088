#!/usr/bin/env python3
"""cpwide - the wide Control Panel on a VGA desktop (SPEC.md 135).

    make && python3 tests/cpwide.py [--shots DIR]

The one row that drives the WIDE face; every row written before 135 that
drives the compact panel on a VGA or VESA desktop is pinned to the compact
face (tests/cpface.py), so without this row nothing would open the wide one.
Boots build/os8088.img on QEMU's VGA (640x480) and checks what the QA pass
of docs/plans/CPWIDE-PLAN.md checked by hand:

  1. the panel opens WIDE: a 618x408 frame on a 640x480 desktop with the
     Dock at the bottom (135.2 step 6, 135.3);
  2. the page list selects pages: Theme, Drivers, Display, Dock, each click
     answered by `cp_sel` - which is also what proves the row's mirror of the
     list's layout (cpw_lay, below) still matches the build;
  3. QA-3, pages own the page block (135.5.1): the Drivers page drawn after
     a visit to Theme, and after one to Display, is pixel for pixel the
     Drivers page drawn first - before the fix Theme's state landed on the
     Drivers page's filter and selection;
  4. QA-1, the face fits the WORK AREA (135.2 step 4): the Dock page (hosted
     compact code) moves the Dock to the Left, the panel is closed, and the
     next open is COMPACT (320 wide) - where the Dock page is in reach to put
     the Dock back at the bottom, after which the next open is wide again.

WHY QEMU (docs/TESTING.md, entry 1's neighbour): this is the shipped VGA
desktop at 640x480 and nothing here is a time; the XT's timings were taken
once on MartyPC and are in the plan (Q1).
"""
import atexit
import os
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)

import dispcp                                                     # noqa: E402
import heapmap                                                    # noqa: E402
import os88qemu                                                   # noqa: E402
import os88sym                                                    # noqa: E402
import shot                                                       # noqa: E402
from os88geom import (KERNEL_SEG, MAX_WIN,                       # noqa: E402
                      TITLE_H, WIN_SIZE, W_FLAGS, W_H, W_TITLE, W_W, W_X, W_Y)

# kernel/ctrl.inc's page records
CP_ISCH, CP_ITIME, CP_IDRV, CP_IVID, CP_ISND = 0, 1, 2, 3, 4
CP_ITHM, CP_IDOCK, CP_IFDD, CP_IKEY = 5, 6, 7, 8
# kernel/cpwide.inc's list: cpw_lay, and the pitches cpw_lbuild lays it at
LAY = [("head",), CP_ISCH, CP_ITIME, CP_IKEY, CP_IFDD,
       ("head",), CP_IDRV, CP_IVID, CP_ISND,
       ("head",), CP_ITHM, CP_IDOCK]
CPW_LT0, CPW_HH, CPW_HG, CPW_RH, CPW_GG = 4, 13, 3, 20, 6
CPW_LW, CPW_PX = 120, 121                   # kernel/cpwlay.inc
WIDE = (618, 408)                           # the frame on 640x480 (135.2)
COMPACT_W = 322                             # cp_tpl's 320 + wm_fit's 2
# the Dock page's radios, HOSTED (135.3), content-relative on the wide face;
# compact, content-relative on the 320-wide panel (kernel/ctrl.inc's page)
DOCK_LEFT_WIDE = (138, 80)
DOCK_BOTTOM_COMPACT = (106, 20)

fails = []


class Q(heapmap.Qmp):
    """heapmap's monitor, with what hmacold.QUI asks of its handle
    (tests/vidxms.py's Q has the same three)."""

    def __init__(self, sock):
        heapmap.Qmp.__init__(self, sock)
        self.sock = sock

    def readseg(self, seg, off, n):
        return self.read(os88qemu.lin(seg, off), n)

    def close(self):
        pass


def check(name, ok, got=""):
    print("  %-60s %s" % (name, "ok" if ok else "FAIL %s" % (got,)))
    sys.stdout.flush()
    if not ok:
        fails.append(name)


def u16(b, i):
    return b[i] | b[i + 1] << 8


def cp_win(q, S):
    """(x, y, w, h) of the Control Panel's frame, or None (dispcp._cp_win's
    match on W_TITLE, with the size)."""
    title = S("cp_ttl") - (KERNEL_SEG << 4)
    wins = q.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
    for i in range(MAX_WIN):
        b = i * WIN_SIZE
        if u16(wins, b + W_FLAGS) & 3 == 3 and u16(wins, b + W_TITLE) == title:
            return (u16(wins, b + W_X), u16(wins, b + W_Y),
                    u16(wins, b + W_W), u16(wins, b + W_H))
    return None


def list_tops(hide):
    """{record: its row's top, content-relative}: cpw_lbuild's walk."""
    tops, y, first = {}, CPW_LT0, True
    for e in LAY:
        if isinstance(e, tuple):
            if not first:
                y += CPW_GG
            first = False
            y += CPW_HH + CPW_HG
        elif e < 8 and hide & (1 << e):
            continue
        else:
            tops[e] = y
            y += CPW_RH
    return tops


def main():
    shots = None
    if "--shots" in sys.argv:
        shots = sys.argv[sys.argv.index("--shots") + 1]
        os.makedirs(shots, exist_ok=True)
    S = os88sym.linear
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    atexit.register(shutil.rmtree, tmp, True)
    sock, _ = os88qemu.launch_test(tmp)
    q = Q(sock)
    hmp = lambda *c: [q.hmp(x) for x in c]                  # noqa: E731
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)

    def zone():
        try:
            return dispcp.drive_ordinal(q, S, "B") is not None
        except Exception:                                   # noqa: BLE001
            return False
    # the boot's own gate (kmain's last act), then the desktop's zones: a
    # menu pressed before kernel_up is answered by a bar still being built
    if not (os88qemu.kernel_up(q, S)
            and os88qemu.acted(q, zone, secs=90, what="the desktop",
                               poll=0.05)):
        raise SystemExit("cpwide: no desktop")
    os88qemu.ui_done(q, S, cap=1.0)

    def click(x, y):
        os88qemu.mouse_click(q, S, hmp, x, y)
        os88qemu.ui_done(q, S, cap=2.0)

    def dump(name):
        ppm = os.path.join(tmp, name + ".ppm")
        q.hmp('screendump "%s"' % ppm)
        w, h, pix = shot.read_ppm(ppm)
        if shots:
            shot.png(os.path.join(shots, name + ".png"), w, h, pix)
        return w, h, pix

    import hmacold
    ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)

    def open_panel():
        # the menu by name, every step confirmed off guest state (tools/
        # os88ui.py's menu_pick on QEMU) - a raw press-drag-release on the
        # chip menu missed about one boot in two here
        ui.menu_pick("Apple", "Control Panel")
        os88qemu.acted(q, lambda: cp_win(q, S) is not None, secs=20,
                       what="the Control Panel window")
        os88qemu.ui_done(q, S, cap=2.0)
        return cp_win(q, S)

    def close_panel(win):
        click(win[0] + 12, win[1] + 9)
        os88qemu.acted(q, lambda: cp_win(q, S) is None, secs=10,
                       what="the Control Panel to close")
        return cp_win(q, S) is None

    sel = lambda: q.read(S("cp_sel"), 1)[0]                 # noqa: E731

    # --- 1. wide ---------------------------------------------------------
    win = open_panel()
    check("the panel opens", win is not None)
    if win is None:
        dump("00-no-panel")
        return 1
    check("...WIDE: a %dx%d frame on 640x480" % WIDE, win[2:] == WIDE,
          "got %dx%d" % win[2:])
    if win[2:] != WIDE:
        return 1
    ox, oy = win[0] + 1, win[1] + TITLE_H + 1
    tops = list_tops(q.read(S("cp_hide"), 1)[0])

    def page(rec, name):
        click(ox + CPW_LW // 2, oy + tops[rec] + CPW_RH // 2)
        os88qemu.acted(q, lambda: sel() == rec, secs=10, what=name)
        check("the list selects %s (cp_sel %d)" % (name, rec), sel() == rec,
              "cp_sel %d" % sel())

    def drivers_area(name):
        w, h, pix = dump(name)
        x0, x1 = ox + CPW_PX, win[0] + win[2] - 2
        y0, y1 = oy, win[1] + win[3] - 2
        return b"".join(pix[(y * w + x0) * 3:(y * w + x1) * 3]
                        for y in range(y0, y1))

    # --- 2 and 3. the list, and pages owning the page block (QA-3) ------
    page(CP_IDRV, "Drivers")
    first = drivers_area("01-drivers")
    page(CP_ITHM, "Theme")
    page(CP_IDRV, "Drivers")
    check("QA-3: Drivers after Theme is Drivers as first drawn",
          drivers_area("02-drivers-after-theme") == first)
    page(CP_IVID, "Display")
    dump("03-display")
    page(CP_IDRV, "Drivers")
    check("QA-3: Drivers after Display is Drivers as first drawn",
          drivers_area("04-drivers-after-display") == first)

    # --- 4. the work area (QA-1) -----------------------------------------
    page(CP_IDOCK, "Dock")
    x0 = lambda: int.from_bytes(q.read(S("vid_band_x0"), 2), "little")  # noqa: E731
    click(ox + DOCK_LEFT_WIDE[0], oy + DOCK_LEFT_WIDE[1])
    os88qemu.acted(q, lambda: x0() != 0, secs=10, what="the Dock on the left")
    check("QA-1: the hosted Dock page puts the Dock on the left", x0() != 0,
          "vid_band_x0 %d" % x0())
    check("the panel closes", close_panel(cp_win(q, S)))
    win = open_panel()
    dump("05-reopen-dock-left")
    check("QA-1: with a side Dock on 640 the next open is COMPACT",
          win is not None and win[2] == COMPACT_W,
          "got %s" % (win,))
    if win is None or win[2] != COMPACT_W:
        return 1
    hide = q.read(S("cp_hide"), 1)[0]
    row = sum(1 for r in range(CP_IDOCK) if not hide & (1 << r))
    click(win[0] + 1 + dispcp.CP_IX + 30,
          win[1] + TITLE_H + 1 + dispcp.CP_I0Y + row * dispcp.CP_IROWH + 7)
    os88qemu.acted(q, lambda: sel() == CP_IDOCK, secs=10, what="Dock")
    check("the compact list selects Dock", sel() == CP_IDOCK,
          "cp_sel %d" % sel())
    click(win[0] + 1 + DOCK_BOTTOM_COMPACT[0],
          win[1] + TITLE_H + 1 + DOCK_BOTTOM_COMPACT[1])
    os88qemu.acted(q, lambda: x0() == 0, secs=10, what="the Dock at the bottom")
    check("...and its Dock page puts the Dock back at the bottom", x0() == 0,
          "vid_band_x0 %d" % x0())
    check("the compact panel closes", close_panel(win))
    win = open_panel()
    dump("06-reopen-dock-bottom")
    check("...and the next open is wide again",
          win is not None and win[2:] == WIDE, "got %s" % (win,))

    q.hmp("quit")
    if fails:
        print("cpwide: FAIL (%d): %s" % (len(fails), ", ".join(fails)))
        return 1
    print("cpwide: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
