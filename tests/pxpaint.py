#!/usr/bin/env python3
"""WHAT PIXEL'S OPERATIONS COST IN PRIMITIVE CALLS (SPEC.md 106.4, 106.14).

    python3 tests/pxpaint.py [--machine os8088_xt_vga] [--record]

CLAUDE.md's rule: a redraw is priced by how many primitive calls it makes.
This counts them - every drawing slot's API cell is a breakpoint for the
length of one operation - and holds each operation to the budget SPEC.md
106.14 records:

  open          File > Revert of a 320x240 8-bit PCX, from the key to the end
                of the decode: the progressive bands, the panels, the status
  zoom step     `=`: the canvas rendered at the next step, the Navigator's
                frame moved, the zoom field - and NOT the panels
  pan step      the down arrow on a picture taller than the canvas: ONE
                OSAPI_GFX_SCROLL and the exposed strip, not the canvas
  pan across    the right arrow: OSAPI_GFX_SAVE/REST band by band and the
                exposed columns
  tool          a tool by its letter: the two buttons whose latch moved
  status        a status field that changed: one run of the cells that did

A change that makes any of them repaint more than it changed fails here
with the counts side by side. --record prints the numbers to put in the
table instead of checking them.
"""
import os, sys, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88build
from pxsyms import pkg_syms, instance, u16

SLOTS = {"fill": 0x0038, "frame": 0x0040, "hline": 0x0028, "vline": 0x0030,
         "fill_gray": 0x0048, "xor_rect": 0x0050, "font_run": 0x01E5,
         "icon_draw": 0x039D, "blit4": 0x0182, "blit1": 0x0320,
         "blitp": 0x03A4, "scroll": 0x019C, "save": 0x03DC, "rest": 0x03E4}
KSEG = 0x60
# THE BUDGET (SPEC.md 106.14): the most each operation may make, on VGA.
# Measured, then rounded up a little: a change past one of these is a
# decision, not a drift.
BUDGET = {
    "open":     {"total": 200, "blitp": 130},
    "zoom":     {"total": 50, "blitp": 45, "fill": 0, "font_run": 3},
    "pan":      {"total": 10, "scroll": 1, "blitp": 7},
    "across":   {"total": 90, "save": 22, "rest": 22},
    "tool":     {"total": 4},
    "status":   {"total": 1, "font_run": 1},
}
FAIL = []


def check(name, ok, detail=""):
    print("   %-58s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_xt_vga")
ap.add_argument("--record", action="store_true")
a = ap.parse_args()
syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run "
             "`make build/pixel.o88`")
DISK = "build/pxpaint.img"
M.scratch_disk(DISK, "build/pixel.o88", "build/PIXEL.GFX",
               "PICS:apps/pixel/samples/CITY.PCX")
print("== PiXEL: primitive calls an operation (SPEC.md 106.14) ==")
with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S
    ui.path("B:/PICS/CITY.PCX")
    M.until(m, lambda _: instance(m, S, image), "PiXEL's instance", poll=0.3,
            limit=90)
    seg = instance(m, S, image)
    base = seg * 16
    B = lambda n: m.read(base + syms[n], 1)[0]
    W = lambda n: u16(m.read(base + syms[n], 2))

    def idle():
        return B("px_busy") == 0 and B("px_job") == 0
    M.until(m, lambda _: W("px_ndone") and idle(), "the decode", poll=0.3,
            limit=900)
    ui.settle()
    ui.mo.to(2, 2)
    ui.settle()
    addrs = [KSEG * 16 + v for v in SLOTS.values()]

    def counted(what, act, wait=None):
        with M.bp_trace(m, *addrs, cap=20000) as tr:
            act()
            if wait:
                M.until(m, lambda _: wait(), what, poll=0.3, limit=900)
            M.ui_done(m, what)
        got = {k: tr.count(tr._by_addr[(KSEG * 16 + v) & 0xFFFFF])
               for k, v in SLOTS.items()}
        got["total"] = sum(got.values())
        return got

    def report(op, got):
        shown = ", ".join("%s %d" % (k, v) for k, v in sorted(got.items())
                          if v and k != "total")
        print("   %-9s %4d calls: %s" % (op, got["total"], shown))
        if a.record:
            return
        for k, lim in BUDGET[op].items():
            check("%s: %s <= %d" % (op, k, lim), got.get(k, 0) <= lim,
                  "%d" % got.get(k, 0))

    def revert():
        m.write(base + syms["px_cur"], b"CITY.PCX".ljust(13, b"\0"))
        m.ctrl("KeyR")
    n0 = W("px_ndone")
    report("open", counted("the open", revert,
                           lambda: W("px_ndone") != n0 and idle()))
    report("zoom", counted("a zoom step", lambda: m.key("Equal")))
    oy = W("px_oy")
    report("pan", counted("a pan down", lambda: m.key("ArrowDown")))
    check("pan: the picture moved", W("px_oy") != oy, "")
    ox = W("px_ox")
    report("across", counted("a pan across", lambda: m.key("ArrowRight")))
    check("across: the picture moved", W("px_ox") != ox, "")
    report("tool", counted("a tool by its letter", lambda: m.key("KeyZ")))
    ui.mo.to(2, 2)
    ui.settle()
    mem = syms["px_sv"] + 6 * 16    # PX_SF_MEM's value: a stale one, so the
    m.write(base + mem, b"x\0")     # next five-second look redraws it - and
    slot = syms["px_slots"] + 6 * syms.get("PX_SLOTSZ", 32) + 8 + 5
    m.write(base + slot, b"x")      # its record told one cell differs, so
                                    # that one cell is what it draws (106.15)
    report("status", counted("a status field",
                             lambda: None,
                             lambda: m.read(base + mem, 1) != b"x"))

print("pxpaint: %s" % ("ok" if not FAIL else "%d FAILED" % len(FAIL)))
sys.exit(1 if FAIL else 0)
