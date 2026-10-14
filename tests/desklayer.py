#!/usr/bin/env python3
"""SPEC.md 11.103: the DESK LAYER - a window under every application window.

    make && make diatest && python3 tests/desklayer.py --leg z
    ...                                               --leg dock
    ...                                               --leg keys
    make small && make diatest && python3 tests/desklayer.py --leg small

`make diatest`'s diadesk360.img is the shipped 360KB system disk plus
DESKPROB.O88 - tests/diaprobe built with -DDP_DESK: header flag OS88_F_DESK,
OSAPI_WM_DESK's QUERY and MAKE in its entry proc, a face of two white cards
with the desktop's ground between them through OSAPI_GFX_DESK, and counters
of its presses, releases and keys in its bss - with a SYSTEM.CFG DIA row and
a DESKPROB.DAT that put it at (375,184). Every assertion is the GUEST'S
state: wm_zord, [menu_win], W_FLAGS, I_FLAGS, the probe's bss, breakpoints,
and the framebuffer.

  --leg z     (row deskz, MartyPC's VGA XT) MAKE took - WF_DESK, WF_OWNBG and
              WF_NOANIM on, WF_SAVEU off, IF_DESK on the record - and the
              face is the cards and the desktop's own dither; a Disk window
              opened over it is ABOVE it in wm_zord and is wm_top; a press on
              the widget's visible part reaches its W_ONCLICK and W_ONMOUSEUP
              with NO wm_raise and NO wm_zcut, leaves the z-order, wm_top and
              the Disk window's overlapping pixels exactly as they were, and
              hands the widget the bar; a press on the Disk window takes the
              bar back; closing the Disk window gives it to LOCATOR, never to
              the widget, and leaves the 1-px column and row BESIDE the
              widget (x+w, y+h) the desktop's dither - a desk window has no
              shadow (SPEC.md 11.103.1); and with only the widget up wm_top
              is 0 and Close Window greys. Every wm_top here is the KERNEL's
              answer, read by calling wm_top on the paused machine.
  --leg dock  (row deskdock) the widget's instance is the TOP slot,
              INST_MAX - 1 (header flag bit 6); the dock strip draws no tile
              for it; a click where its tile would be fronts nothing and
              reaches nothing; the first application opened is tile 0.
  --leg keys  (row deskkeys) Note Pad in front; a press on the widget sends
              the keys to IT while Note Pad stays wm_top, and Note Pad's
              W_ONKEY is never entered; Esc is the widget's RELEASE, which
              gives the bar and the keys back to Note Pad; a press on Note Pad
              takes them too; and a Standard File chooser takes the keys over
              a widget that owns the bar.
  --leg small (row desksmall, MartyPC's 128KB CGA 5150, kern_small) QUERY
              refuses, so the probe is the TITLED FALLBACK: no WF_DESK, an
              ordinary 130 x 115 frame with a title, a dock tile, a
              title-bar drag that moves it, and OSAPI_GFX_DESK still lays the
              desktop's dither in its gaps.

**BREAK IT ON PURPOSE** (each run, SPEC.md 107.9): take wm_lift's desk test
out and `z` fails on wm_zcut firing under the right press; take the
content press's WF_DESK arm out of ui.inc and `z` fails with the press
swallowed (no W_ONCLICK, no release); take wm_top's desk test out and `z`
fails at A, the kernel's own wm_top answering the widget; take
wm_clip_subl's WM_CHROMELESS arm out and `z` fails at E2, the strip beside
the widget left holding the Disk window's pixels; take db_key's IF_DESK test out and `dock`
fails on the tile's pixels; take fdlg_top_x's arm out and `keys` fails with
Note Pad's W_ONKEY entered.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

ap = argparse.ArgumentParser()
ap.add_argument("--leg", choices=("z", "dock", "keys", "small"), required=True)
ARGS = ap.parse_args()

if ARGS.leg == "small":
    import os88build as _B      # noqa: E402
    PKGDIR = _B.at("build")     # the probe and the small disk, in build/
    _B.use_build("build/smallk")    # ...the kernel kern_small's (os88sym)
    os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
else:
    PKGDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))

import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402

TITLE = "DeskProbe"
W_ONKEY = 14                    # apps/os88api.inc
WF_SAVEU, WF_OWNBG, WF_NOANIM = 32, 64, 0x4000
WF_DESK = geom.WF_DESK
IF_DESK = geom.IF_DESK
# the probe's bss (tests/diaprobe/diaprobe.asm, DP_DESK): image + these
DP_CLICKS, DP_UPS, DP_KEYS, DP_DESK, DP_LASTKEY, DP_PAINTS, DP_REL = \
    0, 1, 2, 3, 4, 6, 8
FACE_W, C1, GAP, C2 = 128, 40, 8, 40
FACE_H = C1 + GAP + C2 + GAP

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(PKGDIR, "deskprob.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


class Probe(object):
    def __init__(self, ui):
        self.ui, self.m = ui, ui.m
        self.w = ui.wait_window(TITLE, limit=120)
        self.seg = struct.unpack_from("<H", self.m.read(
            self.rec() + geom.W_SEG, 2))[0]
        self.img = image_size()

    def rec(self):
        return self.ui._S("wm_wins") + self.w.i * geom.WIN_SIZE

    def flags(self):
        return struct.unpack("<H", bytes(self.m.read(self.rec(), 2)))[0]

    def b(self, off):
        return self.m.readseg(self.seg, self.img + off, 1)[0]

    def wd(self, off):
        return struct.unpack("<H", bytes(self.m.readseg(
            self.seg, self.img + off, 2)))[0]

    def win(self):
        return self.ui.window(TITLE)


def zord(ui):
    m = ui.m
    return list(m.read(ui._S("wm_zord"), m.read(ui._S("wm_zn"), 1)[0]))


REGS = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ss", "ds", "es",
        "flags")


def top_i(ui):
    """wm_top's OWN answer, by CALLING it on the paused machine - not
    tools/os88geom.py's top(), the host's re-implementation, which skips
    WF_DESK because it was written to and would keep passing with the
    kernel's desk test gone (the v1 review's finding). tests/drmario.py's
    call shape: park at the routine with IF = 0 (nothing else runs), a near
    return into wm_lift's entry, which nothing reaches with interrupts off,
    and every register, the two stack bytes and CS:IP put back."""
    m = ui.m
    K = geom.KERNEL_SEG << 4
    trap = ui._S("wm_lift")
    m.pause()
    saved = m.regs()
    sp = (saved["sp"] - 2) & 0xFFFF
    slot = (saved["ss"] << 4) + sp
    old = bytes(m.read(slot, 2))
    m.cmd(cmd="park", cs=geom.KERNEL_SEG, ip=ui._S("wm_top") - K)
    for r in REGS:
        m.setreg(r, saved[r])
    m.setreg("ds", geom.KERNEL_SEG)
    m.setreg("sp", sp)
    m.setreg("flags", saved["flags"] & ~0x0300)     # IF and TF off
    m.write(slot, struct.pack("<H", trap - K))
    m.bp_exec(trap)
    m.run()
    hit = m.wait_stop(20) == "breakpoint" and m.status()["ip"] == trap - K
    bx = m.regs()["bx"]
    m.bp_exec()
    m.write(slot, old)
    m.cmd(cmd="park", cs=saved["cs"], ip=saved["ip"])
    for r in REGS:
        m.setreg(r, saved[r])
    m.run()
    if not hit:
        raise SystemExit("the call into wm_top never returned to the trap")
    return None if not bx else (bx + K - ui._S("wm_wins")) // geom.WIN_SIZE


def mirror_top_i(ui):
    """The host mirror's answer, kept beside the kernel's as a cross-check."""
    p = geom.top(ui.m)
    return None if not p else (p - ui._S("wm_wins")) // geom.WIN_SIZE


def menu_win_i(ui):
    p = ui._word("menu_win")            # a KERNEL_SEG offset; _S is linear
    return None if not p else \
        (p + geom.KERNEL_SEG * 16 - ui._S("wm_wins")) // geom.WIN_SIZE


def win_i(ui, off):
    """A window record's KERNEL_SEG offset (I_WIN, [menu_win]) -> its slot."""
    return (off + geom.KERNEL_SEG * 16 - ui._S("wm_wins")) // geom.WIN_SIZE


def bar_names(ui):
    return [t for (t, _, _, _) in ui.menus()]


def rect_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    x1, y1 = max(ax, bx), max(ay, by)
    x2, y2 = min(ax + aw, bx + bw), min(ay + ah, by + bh)
    return (x1, y1, x2 - x1, y2 - y1) if x2 > x1 and y2 > y1 else None


def press(ui, x, y):
    """One click, proven at both edges by os88mouse."""
    ui.mo.click(x, y, settle=0)
    os88marty.guest_sleep(ui.m, 0.6)


def park(ui):
    """The pointer out of every capture: the bare desktop's top-left."""
    ui.mo.to(40, 60)
    os88marty.guest_sleep(ui.m, 0.3)


def visible_point(ui, p, cover):
    """A point on the probe's card 1 that `cover` (x,y,w,h) does not hide."""
    w = p.win()
    fx0 = w.x + 1
    for x in range(fx0 + FACE_W - 6, fx0 + 4, -8):
        for y in (w.y + 6, w.y + C1 - 6):
            if not (cover[0] <= x < cover[0] + cover[2] + 1
                    and cover[1] <= y < cover[1] + cover[3] + 1):
                return x, y
    return None


# =============================================================================
def leg_z(ui):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    print("A: MAKE took, and the face is cards and desktop")
    f = p.flags()
    check(f & WF_DESK and f & WF_OWNBG and f & WF_NOANIM and not f & WF_SAVEU,
          "W_FLAGS %#06x: WF_DESK, WF_OWNBG, WF_NOANIM on, WF_SAVEU off" % f)
    check(p.b(DP_DESK) == 1, "the probe saw MAKE answer CF = 0")
    inst = geom.instances(m)
    mine = [s for s, r in inst.items()
            if win_i(ui, r["win"]) == p.w.i]
    check(mine and inst[mine[0]]["flags"] & IF_DESK,
          "its instance record carries IF_DESK (%r)" % (inst,))
    w = p.win()
    check((w.x, w.y, w.w, w.h) == (375, 184, FACE_W + 2, FACE_H),
          "the frame is (375,184) 130 x %d, chromeless: %r"
          % (FACE_H, (w.x, w.y, w.w, w.h)))
    check(top_i(ui) is None, "with only the widget up the KERNEL's wm_top "
          "answers 0 (Locator)")
    check(mirror_top_i(ui) is None, "...and so does tools/os88geom.py's mirror")
    park(ui)
    fw, fh, fb = m.fbuf()

    def px(x, y):
        return fb[(y * fw + x) * 3]
    white = all(px(x, y) > 128 for y in (w.y, w.y + C1 - 1, w.y + C1 + GAP,
                                          w.y + C1 + GAP + C2 - 1)
                for x in range(w.x + 1, w.x + 1 + FACE_W))
    check(white, "the two cards are white edge to edge")
    gap_rows = [w.y + C1 + 3, w.y + FACE_H - 2]
    dither = all((px(x, y) > 128) == ((x + y) % 2 == 0)
                 for y in gap_rows for x in range(w.x, w.x + w.w))
    cols = all((px(x, y) > 128) == ((x + y) % 2 == 0)
               for x in (w.x, w.x + w.w - 1) for y in range(w.y, w.y + w.h))
    check(dither, "the gaps are the desktop's own (x+y) dither, edge to edge")
    check(cols, "...and so are both ground columns, the frame's full height")

    print("B: a Disk window opened over it is ABOVE it")
    d = ui.open_drive("A")
    ui.settle()
    z = zord(ui)
    check(z and z[0] == p.w.i and z[-1] == d.i,
          "wm_zord %r: the widget at [0], the Disk window on top" % (z,))
    check(top_i(ui) == d.i, "wm_top is the Disk window")
    ov = rect_overlap((d.x, d.y, d.w + 1, d.h + 1), (w.x, w.y, w.w, w.h))
    check(ov is not None, "the Disk window overlaps the widget: %r" % (ov,))
    pt = visible_point(ui, p, (d.x, d.y, d.w, d.h))
    check(pt is not None, "card 1 has a visible point: %r" % (pt,))
    park(ui)
    before = os88marty.crop_rgb(m, *ov) if ov else b""

    print("C: a press on the widget: delivered, and NOTHING raised")
    c0, u0 = p.b(DP_CLICKS), p.b(DP_UPS)
    with os88marty.bp_trace(m, "wm_raise", "wm_zcut") as tr:
        press(ui, *pt)
        ui._wait(lambda: p.b(DP_UPS) != u0, "the widget's W_ONMOUSEUP", 20)
    check(p.b(DP_CLICKS) == (c0 + 1) & 255,
          "its W_ONCLICK ran once (%d -> %d)" % (c0, p.b(DP_CLICKS)))
    check(p.b(DP_UPS) == (u0 + 1) & 255, "...and its W_ONMOUSEUP")
    check(tr.count("wm_raise") == 0 and tr.count("wm_zcut") == 0,
          "no wm_raise and no wm_zcut (%d, %d)"
          % (tr.count("wm_raise"), tr.count("wm_zcut")))
    check(zord(ui) == z, "wm_zord unchanged: %r" % (zord(ui),))
    check(top_i(ui) == d.i, "wm_top is STILL the Disk window")
    check(menu_win_i(ui) == p.w.i, "[menu_win] is the widget: it owns the bar")
    os88marty.guest_sleep(m, 0.5)
    check(TITLE in bar_names(ui), "the bar names it: %r" % (bar_names(ui),))
    park(ui)
    after = os88marty.crop_rgb(m, *ov) if ov else b"?"
    check(after == before, "the Disk window's overlapping pixels are byte "
          "for byte what they were (%d bytes)" % len(before))

    print("C2: a RIGHT press is wm_front's desk arm: activate, never lift")
    press(ui, d.x + 60, d.y + d.h - 40)          # the bar to the Disk window
    ui._wait(lambda: menu_win_i(ui) == d.i, "the Disk window's bar", 20)
    with os88marty.bp_trace(m, "wm_raise", "wm_zcut", "wm_front") as tr:
        ui.mo.to(*pt)
        ui.mo._edge(True, btn=2)
        ui.mo._edge(False, btn=2)
        ui._wait(lambda: menu_win_i(ui) == p.w.i, "the widget's bar", 20,
                 snapshot=lambda: "menu_win = %r" % menu_win_i(ui))
    check(tr.count("wm_front") >= 1, "the right press went through wm_front "
          "(%d)" % tr.count("wm_front"))
    check(tr.count("wm_raise") == 0 and tr.count("wm_zcut") == 0,
          "...which neither raised nor lifted it (%d, %d)"
          % (tr.count("wm_raise"), tr.count("wm_zcut")))
    check(zord(ui) == z and top_i(ui) == d.i,
          "wm_zord %r and wm_top unchanged" % (zord(ui),))

    print("D: a press on the Disk window takes the bar back")
    press(ui, d.x + 60, d.y + d.h - 40)
    ui._wait(lambda: menu_win_i(ui) == d.i, "the Disk window's bar", 20,
             snapshot=lambda: "menu_win = %r" % menu_win_i(ui))
    check(menu_win_i(ui) == d.i, "[menu_win] is the Disk window again")

    print("E: closing the Disk window gives the bar to LOCATOR")
    ui.close(d)
    ui._wait(lambda: menu_win_i(ui) is None, "the bar to go to Locator", 20,
             snapshot=lambda: "menu_win = %r" % menu_win_i(ui))
    check(menu_win_i(ui) is None, "[menu_win] = 0: Locator, not the widget")
    check("Builtins" in bar_names(ui) and TITLE not in bar_names(ui),
          "the bar is Locator's (its Builtins cell): %r" % (bar_names(ui),))
    check(top_i(ui) is None and TITLE in ui.titles(),
          "the widget is up and wm_top is 0")

    print("E2: the strip BESIDE the widget is the desktop's again")
    # SPEC.md 11.103.1: a desk window draws no shadow, so column x+w and row
    # y+h are the DESKTOP's - and the dither's subtractions once took them as
    # a shadow nobody then drew, leaving the Disk window's pixels there.
    park(ui)
    fw, fh, fb = m.fbuf()
    strip = [(w.x + w.w, y) for y in range(w.y, w.y + w.h + 1)] + \
        [(x, w.y + w.h) for x in range(w.x, w.x + w.w)]
    covered = [(x, y) for (x, y) in strip
               if d.x <= x <= d.x + d.w and d.y <= y <= d.y + d.h]
    check(covered, "the Disk window had covered %d pixels of the strip "
          "(the gesture reached it)" % len(covered))
    bad = [(x, y) for (x, y) in strip
           if (fb[(y * fw + x) * 3] > 128) != ((x + y) % 2 == 0)]
    check(not bad, "column %d and row %d are the (x+y) dither (%d of %d off: "
          "%r)" % (w.x + w.w, w.y + w.h, len(bad), len(strip), bad[:6]))

    print("F: with only the widget up, Close Window greys")
    cells = ui.menus()
    fcell = [c for c in cells if c[0] == "File"]
    got = {}
    if check(fcell, "Locator has a File cell"):
        _, x0, x1, _ = fcell[0]
        fx, fy = (x0 + x1) // 2, 8

        def aim(mo):
            os88marty.guest_sleep(m, 0.6)
            got["items"] = ui._word("menu_items_file")
            return fx, fy           # release ON the title: no item picked
        ui.mo.menu(fx, fy, fx, fy, aim=aim)
        os88marty.guest_sleep(m, 0.5)
        check(got.get("items") == ui._S("menu_s_closex") - geom.KERNEL_SEG * 16,
              "File's item 0 is Close Window's MENU_DIS twin (%#x, want %#x)"
              % (got.get("items", 0),
                 ui._S("menu_s_closex") - geom.KERNEL_SEG * 16))


# =============================================================================
def tile_rect(ui, slot):
    x = geom.DOCK_X0 + slot * geom.DOCK_STEP
    y = ui._word("vid_dock_y0") + 3     # the tile row (SPEC.md 30)
    return x, y, geom.DOCK_TILE_W, geom.DOCK_TILE_H


def dark_in(ui, r):
    fw, fh, fb = ui.m.fbuf()
    x, y, w, h = r
    return sum(1 for yy in range(y, y + h) for xx in range(x, x + w)
               if fb[(yy * fw + xx) * 3] < 128)


def leg_dock(ui):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    print("A: the widget's instance is the TOP slot")
    inst = geom.instances(m)
    top = geom.INST_MAX - 1
    check(list(inst) == [top] and inst[top]["flags"] & IF_DESK,
          "the only live record is slot %d, IF_DESK (%r)" % (top, inst))
    print("B: no tile is drawn for it")
    park(ui)
    tr = tile_rect(ui, top)
    check(dark_in(ui, tr) == 0, "tile %d's rect %r is bare strip" % (top, tr))
    t0 = tile_rect(ui, 0)
    check(dark_in(ui, t0) == 0, "...and so is tile 0's: nothing else runs")
    print("C: a click where its tile would be reaches nothing")
    c0 = p.b(DP_CLICKS)
    with os88marty.bp_trace(m, "wm_front", "inst_minimize") as bt:
        press(ui, tr[0] + tr[2] // 2, tr[1] + tr[3] // 2)
        os88marty.guest_sleep(m, 1.0)
    check(bt.count("wm_front") == 0 and bt.count("inst_minimize") == 0,
          "no wm_front, no inst_minimize (%d, %d)"
          % (bt.count("wm_front"), bt.count("inst_minimize")))
    check(p.b(DP_CLICKS) == c0, "the widget's W_ONCLICK was not reached")
    check(menu_win_i(ui) is None, "the bar is still Locator's")
    print("D: the first application is tile 0")
    d = ui.open_drive("A")
    ui.settle()
    inst = geom.instances(m)
    dslot = [s for s, r in inst.items()
             if win_i(ui, r["win"]) == d.i]
    check(dslot == [0], "the Disk window's instance is slot 0 (%r)" % (dslot,))
    park(ui)
    check(dark_in(ui, t0) > 20, "tile 0 is drawn (%d ink pixels)"
          % dark_in(ui, t0))
    check(dark_in(ui, tr) == 0, "tile %d is still bare" % top)


# =============================================================================
def leg_keys(ui):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    print("A: Note Pad in front")
    np = ui.path("A:/APPS/NOTEPAD.O88")
    np = ui.wait_window("Note Pad", limit=60) if np.title != "Note Pad" \
        else np
    for d in [x for x in ui.windows() if x.used and x.title == "Disk"]:
        ui.close(d)
    np = ui.window("Note Pad")
    w = p.win()
    if rect_overlap((np.x, np.y, np.w + 1, np.h + 1), (w.x, w.y, w.w, w.h)):
        pt = visible_point(ui, p, (np.x, np.y, np.w, np.h))
    else:
        pt = (w.x + 60, w.y + 10)
    check(pt is not None, "a visible point on the widget: %r" % (pt,))
    check(top_i(ui) == np.i, "Note Pad is wm_top")
    rec = ui._S("wm_wins") + np.i * geom.WIN_SIZE
    nseg = struct.unpack_from("<H", m.read(rec + geom.W_SEG, 2))[0]
    nkey = struct.unpack_from("<H", m.read(rec + W_ONKEY, 2))[0]
    check(nkey != 0, "Note Pad takes keys (W_ONKEY %#x)" % nkey)
    nk = nseg * 16 + nkey

    print("B: a press on the widget: the keys go to IT")
    press(ui, *pt)
    ui._wait(lambda: menu_win_i(ui) == p.w.i, "the widget to own the bar", 20)
    k0 = p.b(DP_KEYS)
    with os88marty.bp_trace(m, nk) as tr:
        for k in ("KeyT", "ArrowRight", "ArrowDown"):
            m.key(k)
            os88marty.guest_sleep(m, 0.4)
        ui._wait(lambda: p.b(DP_KEYS) == (k0 + 3) & 255, "three keys", 20,
                 snapshot=lambda: "dp_keys %d" % p.b(DP_KEYS))
    check(p.b(DP_KEYS) == (k0 + 3) & 255, "the widget took all three keys")
    check(p.wd(DP_LASTKEY) >> 8 == 0x50, "the last one was Down (AX %#06x)"
          % p.wd(DP_LASTKEY))
    check(tr.n == 0, "Note Pad's W_ONKEY was never entered (%d)" % tr.n)
    check(top_i(ui) == np.i, "...and Note Pad is still wm_top")

    print("C: Esc is RELEASE: the bar and the keys go back")
    m.key("Escape")
    ui._wait(lambda: p.b(DP_REL) != 0, "the widget's RELEASE", 20)
    check(p.b(DP_REL) == 1, "RELEASE answered CF = 0")
    ui._wait(lambda: menu_win_i(ui) == np.i, "Note Pad's bar", 20,
             snapshot=lambda: "menu_win %r" % menu_win_i(ui))
    check(menu_win_i(ui) == np.i, "[menu_win] is Note Pad again")
    k1 = p.b(DP_KEYS)
    with os88marty.bp_trace(m, nk) as tr:
        m.key("KeyQ")
        ui._wait(lambda: tr.n >= 1, "Note Pad's key", 20)
    check(tr.n >= 1 and p.b(DP_KEYS) == k1,
          "the next key is Note Pad's (%d hits, widget %d -> %d)"
          % (tr.n, k1, p.b(DP_KEYS)))

    print("D: a press on Note Pad takes them back too")
    press(ui, *pt)
    ui._wait(lambda: menu_win_i(ui) == p.w.i, "the widget again", 20)
    np = ui.window("Note Pad")
    press(ui, np.x + 30, np.y + np.h - 30)
    ui._wait(lambda: menu_win_i(ui) == np.i, "Note Pad's bar again", 20)
    k2 = p.b(DP_KEYS)
    with os88marty.bp_trace(m, nk) as tr:
        m.key("KeyW")
        ui._wait(lambda: tr.n >= 1, "Note Pad's key", 20)
    check(p.b(DP_KEYS) == k2, "after a press on Note Pad its keys are its own")

    print("E: a Standard File chooser takes the keys over a widget")
    ui.menu_pick("File", "Open...")
    ch = ui.chooser()
    check(ch is not None, "the chooser is up")
    m.write(ui._S("menu_win"),            # the widget's bar, as a press on
            struct.pack("<H", p.rec() - geom.KERNEL_SEG * 16))   # it leaves it
    k3 = p.b(DP_KEYS)
    m.key("ArrowDown")
    os88marty.guest_sleep(m, 1.0)
    check(p.b(DP_KEYS) == k3, "the widget got NO key while the chooser was up "
          "(%d -> %d)" % (k3, p.b(DP_KEYS)))
    ui.chooser_cancel()


# =============================================================================
def leg_small(ui):
    m = ui.m
    print("A: opened by hand on kern_small: QUERY refuses")
    w = ui.path("A:/SYSTEM/DIA/DESKPROB.O88")
    p = Probe(ui)
    ui.settle()
    f = p.flags()
    check(not f & WF_DESK, "no WF_DESK (W_FLAGS %#06x)" % f)
    check(p.b(DP_DESK) == 0, "the probe saw MAKE refused")
    w = p.win()
    check((w.w, w.h) == (FACE_W + 2, FACE_H + geom.TITLE_H + 1),
          "the TITLED fallback, 130 x %d: %r"
          % (FACE_H + geom.TITLE_H + 1, (w.w, w.h)))
    inst = geom.instances(m)
    mine = [s for s, r in inst.items()
            if win_i(ui, r["win"]) == w.i]
    check(mine and not inst[mine[0]]["flags"] & IF_DESK,
          "an ordinary instance: no IF_DESK (%r)" % (inst,))
    park(ui)
    fw, fh, rows = m.vram("cga")
    tx = geom.DOCK_X0 + (mine[0] if mine else 0) * geom.DOCK_STEP
    ty = ui._word("vid_dock_y0") + 3
    ink = sum(1 for yy in range(ty, ty + geom.DOCK_TILE_H)
              for xx in range(tx, tx + geom.DOCK_TILE_W) if not rows[yy][xx])
    check(ink > 20, "it has a dock tile (%d ink pixels)" % ink)
    fx, fy = w.x + 1, w.y + geom.TITLE_H
    gy = fy + C1 + 3
    dither = all((rows[gy][x] != 0) == ((x + gy) % 2 == 0)
                 for x in range(fx, fx + FACE_W))
    check(dither, "OSAPI_GFX_DESK laid the dither in its gap (row %d)" % gy)
    card = all(rows[fy + 5][x] for x in range(fx, fx + FACE_W))
    check(card, "card 1 is white across the face")
    print("B: it is an ordinary window: its title bar drags it")
    ui.drag_window(w, 96, 0)
    w2 = p.win()
    check(w2.x != w.x, "the title-bar drag moved it: %d -> %d" % (w.x, w2.x))


def main():
    tmp = tempfile.mkdtemp(prefix="desklayer-")
    try:
        if ARGS.leg == "small":
            img = os.path.join(tmp, "small360.img")
            shutil.copy(os.path.join(PKGDIR, "small360.img"), img)
            r = subprocess.run(["mcopy", "-o", "-i", img,
                                os.path.join(PKGDIR, "deskprob.o88"),
                                "::/SYSTEM/DIA/DESKPROB.O88"],
                               capture_output=True, text=True)
            if r.returncode:
                sys.exit("mcopy failed: %s" % r.stderr)
            machine = "os8088_5150_cga_128k"
        else:
            img = os.path.join(tmp, "diadesk360.img")
            shutil.copy(os.path.join(PKGDIR, "diadesk360.img"), img)
            machine = "os8088_xt_vga"
        with os88ui.boot(img, machine=machine) as ui:
            {"z": leg_z, "dock": leg_dock, "keys": leg_keys,
             "small": leg_small}[ARGS.leg](ui)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("desk%s: %s" % (ARGS.leg, "PASS" if not fails else
                          "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
