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
              ordinary 130 x 123 frame with a title, a dock tile, a
              title-bar drag that moves it, and OSAPI_GFX_DESK still lays the
              desktop's dither in its gaps.

WAVE 2 (SPEC.md 11.103.6-7) gives the probe a TAG ROW under its cards - a
white plate at rest, a black handle with a white close box while the
kernel's hover edge says WF_HOVER - and these legs:

  --leg drag  (row deskdrag) a press on the tag, a gap and the 1-px ground
              column each posts OSAPI_WM_DESK DRAG and the kernel's ui_drag
              moves it: dy exactly, x snapped, no drag cache, one W_PAINT,
              no W_ONMOUSEUP; a press that does not move paints nothing; a
              drop draws NO title bar (the drag promotes nobody); a drag out
              from under a Disk window leaves no ghost; a DRAG from the
              TIMER is inert; the place is saved.
  --leg hover (row deskhover) the edge, ONE wake, the handle; no edge
              across a gap; the leave; no hover over a covering window and a
              CLIPPED reveal beside it; the edge with a button held and the
              probe's refusal; nothing under a chooser, and the reveal after.
  --leg hovercga / hoverherc (rows deskhovercga, deskhoverherc) the first
              three of those on the 5150's CGA and Hercules, off the 1bpp
              framebuffer's own bits.
  --leg close (row deskclose) the box arms and lights, a slide off cancels,
              the box's place unrevealed drags, and a press and release on it
              closes the widget and removes its DIA row by name.

Wave 2's break-its: take the content press's `je .qdrag` out and `drag`
fails at A on a spurious W_ONDRAG after the drop (the arm is set and
ui_arm_trk finds [ui_dragy] holding the post); turn the pick-up's
`jne .yj1` into a `jmp` and A fails with no move; take ui_drag's WF_DESK
`.nocache` test out and A
fails on wm_dc_take; take the `[wm_dmg_npro]` store out and D fails on a
title bar drawn; take the hover edge's compare out of cur_shape_pass and
`hover` fails at B; take cur_hov_x's chooser test out and F fails.

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
ap.add_argument("--leg", choices=("z", "dock", "keys", "small", "drag",
                                  "hover", "hovercga", "hoverherc", "close"),
                required=True)
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
import os88dia     # noqa: E402
import os88flush   # noqa: E402

TITLE = "DeskProbe"
W_ONKEY = 14                    # apps/os88api.inc
WF_SAVEU, WF_OWNBG, WF_NOANIM = 32, 64, 0x4000
WF_DESK = geom.WF_DESK
IF_DESK = geom.IF_DESK
# the probe's bss (tests/diaprobe/diaprobe.asm, DP_DESK): image + these
DP_CLICKS, DP_UPS, DP_KEYS, DP_DESK, DP_LASTKEY, DP_PAINTS, DP_REL = \
    0, 1, 2, 3, 4, 6, 8
DP_WAKES, DP_REV, DP_XARM, DP_DRAGS, DP_TDRAG, DP_CLOSES, DP_TAGS = \
    9, 10, 11, 12, 13, 14, 15
DP_ONDRAGS = 16
FACE_W, C1, GAP, C2, GAP2, TAG = 128, 40, 8, 40, 4, 12
TY = C1 + GAP + C2 + GAP2       # the tag row's first face row (wave 2)
FACE_H = TY + TAG
PX, PW, XX, XW = 32, 64, 3, 10  # the rest plate; the handle's close box
# os88dia.inc's bss after the probe's own 32 bytes (DIA_BSS), through the
# host tool's own arithmetic
DP_DIA_BSS = 32                 # the probe's DIA_BSS (diaprobe.asm, DP_DESK)
DIA_HDR = os88dia.dia_hdr_off(DP_DIA_BSS)
DIA_ST = os88dia.dia_st_off(DP_DIA_BSS, 0)
DIA_S_ROW, DIA_S_DIRTY = 2, 16

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
    gap_rows = [w.y + C1 + 3, w.y + C1 + GAP + C2 + 1]
    dither = all((px(x, y) > 128) == ((x + y) % 2 == 0)
                 for y in gap_rows for x in range(w.x, w.x + w.w))
    cols = all((px(x, y) > 128) == ((x + y) % 2 == 0)
               for x in (w.x, w.x + w.w - 1) for y in range(w.y, w.y + w.h))
    check(dither, "the gaps are the desktop's own (x+y) dither, edge to edge")
    check(cols, "...and so are both ground columns, the frame's full height")

    print("B: a Disk window opened over it is ABOVE it")
    d = ui.open_drive("A")
    ui.settle()
    if d.y + d.h <= w.y + w.h:          # ...and over the strip below it, which
        ui.move_window(d, d.x, w.y + w.h + 8 - d.h)     # E2 reads (the tag row
        ui.settle()                     # made the face taller than the fixture
        d = ui.window("Disk")           # Disk window reaches)
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


# =============================================================================
# WAVE 2 (SPEC.md 11.103.6-7): the posted drag, the hover edge, the close
# =============================================================================
class Screen(object):
    """One read of the glass: `lit(x, y)`. VGA through the card's rendered
    framebuffer (mode 12h has no flat memory), 1bpp through its own bits."""
    def __init__(self, m):
        vt = m.cmd(cmd="video")["type"]
        if vt in ("cga", "mda", "hercules"):
            _, _, rows = m.vram()
            self.lit = lambda x, y: rows[y][x] != 0
        else:
            fw, _, fb = m.fbuf()
            self.lit = lambda x, y: fb[(y * fw + x) * 3] > 128


def inside(x, y, r):
    return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]


def tag_bad(m, w, state, cover=()):
    """How many of the tag row's pixels are NOT the `state` picture ('rest':
    the white plate on the desktop's dither; 'handle': black, the close box's
    interior white), the pixels under any `cover` (x,y,w,h) skipped."""
    scr = Screen(m)
    fx0, ty = w.x + 1, w.y + TY
    bad, n = [], 0
    for r in range(TAG):
        y = ty + r
        for c in range(FACE_W):
            x = fx0 + c
            if any(inside(x, y, cv) for cv in cover):
                continue
            if state == "handle":
                want = XX <= c < XX + XW and 1 <= r <= TAG - 2
            else:
                want = True if PX <= c < PX + PW else (x + y) % 2 == 0
            n += 1
            if scr.lit(x, y) != want:
                bad.append((x, y))
    return bad, n


def face_bad(m, w, cover=()):
    """The cards white and both gaps the desktop's dither, at the frame's
    place NOW - a drop that left a ghost of what covered it fails here."""
    scr = Screen(m)
    fx0 = w.x + 1
    bad = []
    rows = [(w.y + r, "card") for r in (0, C1 // 2, C1 - 1)] + \
        [(w.y + C1 + r, "gap") for r in (0, GAP - 1)] + \
        [(w.y + C1 + GAP + r, "card") for r in (0, C2 - 1)] + \
        [(w.y + C1 + GAP + C2 + r, "gap") for r in (0, GAP2 - 1)]
    for y, kind in rows:
        for x in range(fx0, fx0 + FACE_W):
            if any(inside(x, y, cv) for cv in cover):
                continue
            want = True if kind == "card" else (x + y) % 2 == 0
            if scr.lit(x, y) != want:
                bad.append((x, y))
    for x in (w.x, w.x + w.w - 1):          # both ground columns
        for y in range(w.y, w.y + w.h):
            if not any(inside(x, y, cv) for cv in cover) and \
                    scr.lit(x, y) != ((x + y) % 2 == 0):
                bad.append((x, y))
    return bad


def hovw(ui):
    """[cur_hovw] as a window SLOT, None for 0."""
    v = ui._word("cur_hovw")
    return None if not v else win_i(ui, v)


def lock_free(ui):
    return ui.m.read(ui._S("gfx_lock_flag"), 1)[0] == 0


def wdrag(ui, p, src, dx, dy):
    """Press at `src`, walk by (dx, dy) with the button held, release - and
    wait for ui_drag to be OVER (the lock free), os88ui.move_window's rule."""
    x, y = src
    ui.mo.drag(x, y, x + dx, y + dy, settle=0)
    ui._wait(lambda: lock_free(ui), "the drag to finish (the lock free)", 30)
    os88marty.guest_sleep(ui.m, 0.5)


def gap_point(w):
    return w.x + 1 + 60, w.y + C1 + GAP // 2


def tag_point(w):
    """On the tag row, right of the close box and left of the plate."""
    return w.x + 1 + 20, w.y + TY + TAG // 2


def box_point(w):
    return w.x + 1 + XX + XW // 2, w.y + TY + TAG // 2


def dat(ui, m, tmp):
    try:
        return os88dia.parse_dat(volume(m, tmp).read(
            "SYSTEM/APPDATA/DESKPROB.DAT"), 0, 1)
    except Exception as e:      # noqa: BLE001 - reported, not hidden
        return {"pos": False, "x": None, "y": None, "err": str(e)}


def volume(m, tmp, drive=0):
    out = os.path.join(tmp, "flushed%d.img" % drive)
    m.pause()
    try:
        m.flush(drive, out)
    finally:
        m.run()
    return os88flush.Volume(open(out, "rb").read())


def leg_drag(ui, tmp):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    w0 = p.win()
    print("A: a press on the TAG posts DRAG, and the kernel moves it")
    park(ui)
    c0, u0, d0, n0 = p.b(DP_CLICKS), p.b(DP_UPS), p.b(DP_DRAGS), \
        p.wd(DP_PAINTS)
    o0 = p.b(DP_ONDRAGS)
    with os88marty.bp_trace(m, "ui_drag", "wm_dc_take") as tr:
        wdrag(ui, p, tag_point(w0), 43, 33)
    w1 = p.win()
    check(p.b(DP_DRAGS) == (d0 + 1) & 255, "the probe posted ONE DRAG")
    check(tr.count("ui_drag") == 1, "ui_drag ran once (%d)" % tr.count("ui_drag"))
    check(tr.count("wm_dc_take") == 0, "...and took NO drag cache (%d): a desk "
          "window is .nocache" % tr.count("wm_dc_take"))
    check(w1.y == w0.y + 33, "W_Y moved by the odd 33: %d -> %d" % (w0.y, w1.y))
    check(w1.x % 8 == 7 and abs(w1.x - w0.x - 43) <= 7,
          "W_X %d = 7 mod 8, within the snap of +43 from %d" % (w1.x, w0.x))
    check(p.wd(DP_PAINTS) == (n0 + 1) & 0xFFFF,
          "the drop cost ONE W_PAINT (%d -> %d)" % (n0, p.wd(DP_PAINTS)))
    check(p.b(DP_CLICKS) == c0 and p.b(DP_UPS) == u0,
          "no card press and NO W_ONMOUSEUP: the drag ate the release")
    check(ui._word("ui_armw") == 0, "[ui_armw] = 0: nothing left armed")
    park(ui)
    os88marty.guest_sleep(m, 0.4)
    check(p.b(DP_ONDRAGS) == o0, "...and NO W_ONDRAG after the drop, the "
          "pointer moving (%d -> %d): the press armed nothing"
          % (o0, p.b(DP_ONDRAGS)))
    park(ui)
    bad = face_bad(m, w1)
    check(not bad, "at the new place the cards are white and every gap pixel "
          "the (x+y) dither at its own screen coordinate (%d off: %r)"
          % (len(bad), bad[:6]))
    scr = Screen(m)
    vac = [(x, y) for y in range(w0.y, w1.y) for x in range(w0.x, w0.x + w0.w)
           if scr.lit(x, y) != ((x + y) % 2 == 0)]
    check(not vac, "the strip it vacated is the desktop's dither (%d off)"
          % len(vac))

    print("B: from a GAP, and from the LEFT GROUND COLUMN, back")
    wdrag(ui, p, gap_point(w1), -43, -33)
    w2 = p.win()
    check((w2.x, w2.y) == (w0.x, w0.y) or w2.y == w0.y,
          "a gap drags it back: (%d,%d)" % (w2.x, w2.y))
    wdrag(ui, p, (w2.x, w2.y + 20), 16, 8)
    w3 = p.win()
    check((w3.x, w3.y) == (w2.x + 16, w2.y + 8),
          "the 1-px ground column drags it: (%d,%d) -> (%d,%d)"
          % (w2.x, w2.y, w3.x, w3.y))
    check(p.b(DP_DRAGS) == (d0 + 3) & 255, "three DRAGs posted in all")

    print("C: a press that does not move repaints nothing")
    gp = gap_point(w3)
    ui.mo.to(*gp)
    os88marty.guest_sleep(m, 0.6)           # the hover edge's reveal, first
    n1 = p.wd(DP_PAINTS)
    with os88marty.bp_trace(m, "wm_paint_dmg", "ui_drag") as tr:
        ui.mo._sep()
        ui.mo._edge(True)
        ui.mo._edge(False)
        ui._wait(lambda: lock_free(ui), "the drag to finish", 30)
        os88marty.guest_sleep(m, 0.6)
    check(tr.count("ui_drag") == 1 and tr.count("wm_paint_dmg") == 0,
          "ui_drag ran and wm_paint_dmg did not (%d, %d)"
          % (tr.count("ui_drag"), tr.count("wm_paint_dmg")))
    check(p.wd(DP_PAINTS) == n1 and (p.win().x, p.win().y) == (w3.x, w3.y),
          "no W_PAINT, and the frame did not move")

    print("D: the drag promotes nobody - the front window keeps its title")
    d = ui.open_drive("A")
    ui.settle()
    d = ui.window("Disk")
    w3 = p.win()
    if rect_overlap((d.x, d.y, d.w + 1, d.h + 1), (w3.x, w3.y, w3.w, w3.h)):
        ui.move_window(d, 8, 30)
        d = ui.window("Disk")
    check(not rect_overlap((d.x, d.y, d.w + 1, d.h + 1),
                           (w3.x - 48, w3.y - 48, w3.w + 96, w3.h + 96)),
          "the Disk window is clear of the widget: %r" % ((d.x, d.y, d.w, d.h),))
    check(top_i(ui) == d.i, "...and it is wm_top")
    with os88marty.bp_trace(m, "wm_draw_title", "ui_drag") as tr:
        wdrag(ui, p, gap_point(w3), 40, 0)
    check(tr.count("ui_drag") == 1, "the widget was dragged")
    check(tr.count("wm_draw_title") == 0, "...and NO title bar was drawn "
          "(%d): the front window's was right already (SPEC.md 11.103.6)"
          % tr.count("wm_draw_title"))

    print("E: a drag out from under a covering window leaves no ghost")
    w4 = p.win()
    ui.move_window(d, max(0, w4.x + 60 - d.w), w4.y + 30)  # its left part
    d = ui.window("Disk")
    ov = rect_overlap((d.x, d.y, d.w + 1, d.h + 1), (w4.x, w4.y, w4.w, w4.h))
    check(ov is not None, "the Disk window covers part of it: %r" % (ov,))
    park(ui)
    before = os88marty.crop_rgb(m, d.x, d.y, d.w, d.h)
    src = (w4.x + w4.w - 2, w4.y + C1 + GAP // 2)       # a gap, right end
    check(not inside(src[0], src[1], (d.x, d.y, d.w + 1, d.h + 1)),
          "the grab point %r is the widget's" % (src,))
    with os88marty.bp_trace(m, "wm_dc_take", "wm_draw_win") as tr:
        wdrag(ui, p, src, 0, -(w4.y - 30))
    w5 = p.win()
    park(ui)
    check(tr.count("wm_dc_take") == 0, "no drag cache taken")
    # THE PRICE OF A DROP UNDER A COVERING WINDOW (SPEC.md 11.103.6): ui_drag's
    # drop was written for a FRONT window and a widget is the BACKMOST, so the
    # Disk window over its old rect is marked stale and redrawn whole - chrome,
    # title bar and W_PAINT - over pixels that were already right. Measured
    # rather than refused: the cure is resident bytes kern_big does not have
    # (107.9's owner item), and this count is what says it is still paid.
    print("   a drop under the Disk window ran wm_draw_win %d times "
          "(the widget, and the Disk window redrawn whole)"
          % tr.count("wm_draw_win"))
    check(tr.count("wm_draw_win") == 2, "wm_draw_win twice on the drop: the "
          "widget and the covering Disk window (%d)" % tr.count("wm_draw_win"))
    cover = [(d.x, d.y, d.w + 1, d.h + 1)]
    bad = face_bad(m, w5, cover)
    check(not bad, "the face at (%d,%d) carries none of the Disk window "
          "(%d off: %r)" % (w5.x, w5.y, len(bad), bad[:6]))
    after = os88marty.crop_rgb(m, d.x, d.y, d.w, d.h)
    check(after == before, "the Disk window is byte for byte as it was")
    ui.close(d)

    print("F: a DRAG posted from the TIMER is inert")
    w6 = p.win()
    m.write(p.seg * 16 + p.img + DP_TDRAG, b"\x01")
    ui._wait(lambda: p.b(DP_TDRAG) == 2, "the beat's DRAG", 30)
    wrec = p.rec() - geom.KERNEL_SEG * 16       # the record's KERNEL_SEG offset
    check(ui._word("ui_dragy") == wrec,
          "the timer's post is in [ui_dragy]: the widget's record %#x (%#x)"
          % (wrec, ui._word("ui_dragy")))
    c1, u1 = p.b(DP_CLICKS), p.b(DP_UPS)
    with os88marty.bp_trace(m, "ui_drag") as tr:
        press(ui, w6.x + 60, w6.y + 10)                 # card 1
        ui._wait(lambda: p.b(DP_UPS) != u1, "the card's W_ONMOUSEUP", 20)
    check(tr.count("ui_drag") == 0, "the next press is NOT a drag (%d)"
          % tr.count("ui_drag"))
    check(p.b(DP_CLICKS) == (c1 + 1) & 255 and p.b(DP_UPS) == (u1 + 1) & 255,
          "...it is a click: W_ONCLICK and W_ONMOUSEUP once each")
    check((p.win().x, p.win().y) == (w6.x, w6.y), "and the frame stayed put")

    print("G: the place is saved")
    w7 = p.win()
    hdr = lambda: struct.unpack("<HH", m.readseg(p.seg, p.img + DIA_HDR + 4, 4))
    st = lambda: m.readseg(p.seg, p.img + DIA_ST, 1)[0]
    ui._wait(lambda: hdr() == (w7.x, w7.y) and not st() & DIA_S_DIRTY,
             "the drag's save", 120,
             snapshot=lambda: "header %r, dia_st %#x" % (hdr(), st()))
    dd = dat(ui, m, tmp)
    check(dd["pos"] and (dd["x"], dd["y"]) == (w7.x, w7.y),
          "DESKPROB.DAT on A: holds the frame's place %r (%r)"
          % ((w7.x, w7.y), dd))


def leg_hover(ui, tmp, full=True):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    w = p.win()
    print("A: at rest - no hover, the plate")
    park(ui)
    check(not p.flags() & geom.WF_HOVER, "no WF_HOVER (W_FLAGS %#06x)" % p.flags())
    check(p.b(DP_REV) == 0, "the probe shows the rest plate")
    bad, n = tag_bad(m, w, "rest")
    check(n and not bad, "the tag row is the white plate on the desktop's "
          "dither (%d of %d off: %r)" % (len(bad), n, bad[:6]))

    print("B: the pointer on a GAP: the edge, the wake, the handle")
    wk0 = p.b(DP_WAKES)
    ui.mo.to(*gap_point(w))
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle", 20,
             snapshot=lambda: "W_FLAGS %#06x wakes %d"
             % (p.flags(), p.b(DP_WAKES)))
    check(p.flags() & geom.WF_HOVER, "WF_HOVER is set")
    check(hovw(ui) == p.w.i, "[cur_hovw] names the widget (%r)" % hovw(ui))
    check(p.b(DP_WAKES) == (wk0 + 1) & 255, "ONE wake (%d -> %d)"
          % (wk0, p.b(DP_WAKES)))
    os88marty.guest_sleep(m, 0.4)
    bad, n = tag_bad(m, w, "handle")
    check(not bad, "the tag row is the handle: black, the close box white "
          "(%d of %d off: %r)" % (len(bad), n, bad[:6]))
    print("B2: card, gap, card - one window, so NO further edge")
    for pt in ((w.x + 40, w.y + 10), gap_point(w), (w.x + 40, w.y + C1 + GAP + 10)):
        ui.mo.to(*pt)
        os88marty.guest_sleep(m, 0.3)
    check(p.b(DP_WAKES) == (wk0 + 1) & 255, "still one wake (%d)"
          % p.b(DP_WAKES))

    print("C: the pointer off: the leave, the plate back")
    park(ui)
    ui._wait(lambda: p.b(DP_REV) == 0, "the rest plate", 20)
    check(not p.flags() & geom.WF_HOVER, "WF_HOVER is clear")
    check(hovw(ui) is None, "[cur_hovw] is the desktop (0)")
    check(p.b(DP_WAKES) == (wk0 + 2) & 255, "the second wake")
    os88marty.guest_sleep(m, 0.4)
    bad, n = tag_bad(m, w, "rest")
    check(not bad, "the plate again, every pixel (%d off)" % len(bad))
    if not full:
        return

    print("D: over a COVERING window there is no hover")
    d = ui.open_drive("A")
    ui.settle()
    d = ui.window("Disk")
    ui.move_window(d, max(0, w.x + 50 - d.w), w.y + 40)    # its left part
    d = ui.window("Disk")
    ov = rect_overlap((d.x, d.y, d.w + 1, d.h + 1), (w.x, w.y, w.w, w.h))
    check(ov is not None, "the Disk window covers part of it: %r" % (ov,))
    park(ui)
    before = os88marty.crop_rgb(m, *ov)
    wk1 = p.b(DP_WAKES)
    ui.mo.to(ov[0] + ov[2] // 2, ov[1] + ov[3] // 2)
    os88marty.guest_sleep(m, 0.6)
    check(not p.flags() & geom.WF_HOVER and p.b(DP_REV) == 0,
          "on the Disk window over it: no WF_HOVER, no handle")
    check(hovw(ui) == d.i, "[cur_hovw] is the Disk window")
    check(p.b(DP_WAKES) == wk1, "and the widget was not woken")
    vis = (w.x + w.w - 20, w.y + C1 + GAP // 2)
    check(not inside(vis[0], vis[1], (d.x, d.y, d.w + 20, d.h + 20)),
          "a visible gap point %r clear of the Disk window" % (vis,))
    ui.mo.to(*vis)
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle on the visible part", 20)
    after = os88marty.crop_rgb(m, *ov)
    check(after == before, "the reveal was CLIPPED: the Disk window's pixels "
          "over the widget are as they were (%d bytes)" % len(before))
    cover = [(d.x, d.y, d.w + 1, d.h + 1)]
    bad, n = tag_bad(m, w, "handle", cover)
    check(not bad, "the visible part of the tag row is the handle (%d of %d "
          "off)" % (len(bad), n))
    park(ui)
    ui._wait(lambda: p.b(DP_REV) == 0, "the plate", 20)
    ui.close(d)

    print("E: a BUTTON HELD: the kernel reports the edge, the probe refuses")
    c1 = p.b(DP_CLICKS)
    ui.mo.to(w.x + 40, w.y + 10)                    # card 1: revealed
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle", 20)
    ui.mo._sep()
    ui.mo._edge(True)                               # ...pressed, and held
    ui._wait(lambda: p.b(DP_CLICKS) != c1, "the card press", 20)
    ui.mo.to(40, 60, l=True)                        # off, held
    ui._wait(lambda: not p.flags() & geom.WF_HOVER and p.b(DP_REV) == 0,
             "the leave with the button held", 20)
    wk2 = p.b(DP_WAKES)
    ui.mo.to(*gap_point(w), l=True)                 # back on, held
    ui._wait(lambda: p.flags() & geom.WF_HOVER, "the re-entry's edge", 20)
    os88marty.guest_sleep(m, 0.6)
    check(p.b(DP_WAKES) == (wk2 + 1) & 255, "the re-entry woke the widget")
    check(p.b(DP_REV) == 0, "...which did NOT reveal: a button is held")
    ui.mo._edge(False)
    os88marty.guest_sleep(m, 0.4)
    park(ui)
    ui._wait(lambda: not p.flags() & geom.WF_HOVER, "the leave", 20)

    print("F: under a Standard File chooser nothing is hovered")
    wdrag(ui, p, gap_point(w), -(w.x - 7), -(w.y - 30))     # out of its way
    w = p.win()
    np = ui.path("A:/APPS/NOTEPAD.O88")             # an application with a
    np = ui.wait_window("Note Pad", limit=60) \
        if np.title != "Note Pad" else np            # File > Open
    for dw in [x for x in ui.windows() if x.used and x.title == "Disk"]:
        ui.close(dw)
    np = ui.window("Note Pad")
    park(ui)
    ui.menu_pick("File", "Open...")
    ch = ui.chooser()
    covers = [(np.x, np.y, np.w + 1, np.h + 1), (ch.x, ch.y, ch.w + 1, ch.h + 1)]
    cands = [(x, y) for y in (w.y + C1 + GAP // 2, w.y + C1 // 2)
             for x in range(w.x + 4, w.x + w.w - 4, 6)
             if not any(inside(x, y, c) or inside(x + 12, y + 20, c)
                        for c in covers)]
    vis = cands[0] if cands else None
    check(vis is not None, "a widget point neither Note Pad %r nor the "
          "chooser %r covers" % (covers[0], covers[1]))
    if vis is None:
        return
    wk3 = p.b(DP_WAKES)
    ui.mo.to(*vis)
    os88marty.guest_sleep(m, 0.8)
    check(not p.flags() & geom.WF_HOVER and p.b(DP_REV) == 0,
          "on the widget under the chooser: no WF_HOVER, no handle")
    check(hovw(ui) is None and p.b(DP_WAKES) == wk3,
          "[cur_hovw] 0 and no wake (%r, %d)" % (hovw(ui), p.b(DP_WAKES)))
    ui.chooser_cancel()
    ui._wait(lambda: ui._word("fdlg_win") == 0, "the chooser reaped", 30)
    ui.mo.to(vis[0] + 2, vis[1])                    # the first move after
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle once the chooser is gone",
             20)
    check(p.flags() & geom.WF_HOVER, "...and WF_HOVER")


def leg_close(ui, tmp):
    m = ui.m
    p = Probe(ui)
    ui.settle()
    st = lambda: m.readseg(p.seg, p.img + DIA_ST, 1)[0]
    ui._wait(lambda: st() & DIA_S_ROW, "the probe's registration", 90)
    w = p.win()
    cfg = os88dia.parse_cfg(volume(m, tmp).read("SYSTEM.CFG"))
    check([r for r in cfg["rows"] if r and r["dia"]],
          "SYSTEM.CFG on A: carries its DIA row before the close")
    print("A: press the box, slide off, release: still open")
    ui.mo.to(*box_point(w))
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle", 20)
    ui.mo._sep()
    ui.mo._edge(True)
    ui._wait(lambda: p.b(DP_XARM) == 1, "the box armed", 20)
    os88marty.guest_sleep(m, 0.3)
    scr = Screen(m)
    bx0, by0 = w.x + 1 + XX, w.y + TY + 1
    hx, hy = box_point(w)               # the arrow hangs right of and below
    left = [(x, y) for y in range(by0, by0 + TAG - 2)   # its hot spot
            for x in range(bx0, bx0 + XW) if x < hx or y < hy]
    lit = sum(1 for (x, y) in left if not scr.lit(x, y))
    check(left and lit == len(left), "the box is LIT: its interior clear of "
          "the arrow is dark (%d of %d)" % (lit, len(left)))
    ui.mo.to(40, 60, l=True)
    ui.mo._edge(False)
    ui._wait(lambda: p.b(DP_XARM) == 0, "the release", 20)
    os88marty.guest_sleep(m, 0.6)
    check(p.b(DP_CLOSES) == 0 and "DeskProbe" in ui.titles(),
          "slid off: nothing closed, the widget is up")
    check(p.b(DP_REV) == 0, "...and its handle went with the pointer")
    bad, _ = tag_bad(m, w, "rest")
    check(not bad, "the rest plate, every pixel (%d off)" % len(bad))

    print("B: a press on the box's place while NOT revealed drags")
    ui.mo.to(*box_point(w))
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle", 20)
    m.write(p.seg * 16 + p.img + DP_REV, b"\x00")      # the probe's own word:
    d0 = p.b(DP_DRAGS)                                 # "not revealed"
    ui.mo._sep()
    ui.mo._edge(True)
    ui.mo._edge(False)
    ui._wait(lambda: lock_free(ui), "the drag to finish", 30)
    os88marty.guest_sleep(m, 0.5)
    check(p.b(DP_DRAGS) == (d0 + 1) & 255 and p.b(DP_XARM) == 0,
          "it posted a DRAG and armed nothing")
    check("DeskProbe" in ui.titles(), "the widget is still up")
    park(ui)                                           # the picture right again
    ui._wait(lambda: not p.flags() & geom.WF_HOVER, "the leave", 20)

    print("C: press and release ON the box: the widget closes")
    ui.mo.to(*box_point(w))
    ui._wait(lambda: p.b(DP_REV) == 1, "the handle", 20)
    ui.mo._sep()
    ui.mo._edge(True)
    ui._wait(lambda: p.b(DP_XARM) == 1, "the box armed", 20)
    ui.mo._edge(False)
    ui._wait(lambda: "DeskProbe" not in ui.titles(), "the widget to close", 30)
    check("DeskProbe" not in ui.titles(), "the window is gone")
    inst = geom.instances(m)
    check(geom.INST_MAX - 1 not in inst, "its instance is gone (%r)" % (inst,))
    os88marty.guest_sleep(m, 1.0)
    cfg = os88dia.parse_cfg(volume(m, tmp).read("SYSTEM.CFG"))
    rows = [r for r in cfg["rows"] if r and r["dia"]]
    check(not rows, "SYSTEM.CFG holds no DIA row: removed by name (%r)"
          % (rows,))


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
            machine = {"hovercga": os88marty.machine("os8088_5150_cga"),
                       "hoverherc": "os8088_5150_herc_gla"}.get(
                           ARGS.leg, "os8088_xt_vga")
        with os88ui.boot(img, machine=machine) as ui:
            {"z": leg_z, "dock": leg_dock, "keys": leg_keys,
             "small": leg_small,
             "drag": lambda u: leg_drag(u, tmp),
             "hover": lambda u: leg_hover(u, tmp),
             "hovercga": lambda u: leg_hover(u, tmp, full=False),
             "hoverherc": lambda u: leg_hover(u, tmp, full=False),
             "close": lambda u: leg_close(u, tmp)}[ARGS.leg](ui)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("desk%s: %s" % (ARGS.leg, "PASS" if not fails else
                          "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
