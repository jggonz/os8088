#!/usr/bin/env python3
"""THE GESTURE, ON THE GLASS (SPEC.md 13.7, 13.8, 20.5.1.3).

The four things a standard button must do, driven on a real emulated 8088:
it goes DOWN while held, comes back UP when the pointer slides off it, goes
down again on the way back, and does NOT act when the release lands somewhere
else.  Telnet's Connect is the subject because it is a plain single-segment
package with one button and a caption that names its own state.

THE FIRST ASSERTION IS THE ONE THAT MATTERS: while the button is held,
te_state is still 0.  Under the press-fired code this replaces, the action had
already run by then - which is the whole of the defect
docs/plans/BUTTON-GESTURE-PLAN.md is about.

WHY THIS CANNOT BE A SCREENSHOT ROW: a press-fired button and a release-fired
one photograph identically.  tests/unit/t_btnrules.py is the static half and
is what catches a NEW offender; this is what proves the mechanism works.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT + "/tools")
import os88ui, os88marty, os88geom

def u16(b): return b[0] | (b[1] << 8)


def pkg_syms(src=None, inc=None):
    """A package's own symbols, by re-assembling it with a map - atclip's
    trick.  THE DEFINES MUST MATCH THE SHIPPED BUILD: the DOS box on the
    system disk is dosp.bin (-DDOSKPART -DDOS_EXTCORE), and a map taken
    without them resolves to plausible, wrong offsets."""
    import subprocess, tempfile
    src = src or (ROOT + "/apps/telnet/telnet.asm")
    inc = inc or ["-I", ROOT + "/apps/", "-I", ROOT + "/apps/telnet/",
                  "-I", ROOT + "/drivers/net/"]
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "t.asm"), os.path.join(d, "t.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + inc +
                       ["-o", os.path.join(d, "t.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


syms = pkg_syms()

with os88ui.boot(ROOT + "/build/os8088-360.img",
                 apps=ROOT + "/build/apps360.img") as ui:
    m, mo = ui.m, ui.mo
    w = ui.path("A:/APPS/TELNET.O88")
    seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
    ui.settle()

    rect = [u16(m.readseg(seg, syms["te_btn"] + i*2, 2)) for i in range(4)]
    down = lambda: u16(m.readseg(seg, syms["te_btrec"] + 10, 2))   # BT_DOWN
    state = lambda: m.readseg(seg, syms["te_state"], 1)[0]
    cx, cy = (rect[0]+rect[2])//2, (rect[1]+rect[3])//2
    print("Connect rect %s centre (%d,%d) state=%d\n" % (rect, cx, cy, state()))

    res = []
    mo.to(cx, cy); os88marty.settle(m)
    mo._edge(True); m.advance(frames=8); m.run()
    res.append(("press and HOLD", down(), 1))

    mo.to(cx, max(2, rect[1]-30), l=True); m.advance(frames=8); m.run()
    res.append(("slide OFF it", down(), 0))

    mo.to(cx, cy, l=True); m.advance(frames=8); m.run()
    res.append(("slide BACK on", down(), 1))

    mo.to(cx, max(2, rect[1]-30), l=True); m.advance(frames=8); m.run()
    mo._edge(False); os88marty.settle(m)
    res.append(("release OFF -> no fire", state(), 0))
    res.append(("...and it is UP again", down(), 0))

    ok = True
    for i, (what, got, want) in enumerate(res, 1):
        good = got == want
        ok &= good
        print("%d. %-26s got %-4d want %-4d %s" % (i, what, got, want,
                                                   "PASS" if good else "FAIL"))
    # --- ...AND DOS's RECORD IS AIMED ON THE PAINT PATH ---------------------
    # The gesture above proves the four EDGES. It cannot prove a record is
    # AIMED, and that is the half that shipped broken: DOS aimed its record in
    # its CLICK path, so a plain paint ran with BT_N = 0, os88ui_btn drew
    # nothing for an index past a live count of zero, and two buttons were
    # simply absent. Every static check passed and every still looked like a
    # window with no buttons in it, which is not obviously wrong.
    #
    # So this opens the DOS box - which nothing above touches, and which is
    # PAINTED and never clicked here - and reads the record the painter was
    # supposed to aim. BT_N is the exact byte that was zero.
    ui.menu_pick("Telnet", "Close")
    ui.settle()
    dw = ui.path("A:/APPS/DOS.O88")
    ui.settle()
    dsyms = pkg_syms(ROOT + "/apps/dos/dos.asm",
                     ["-I", ROOT + "/apps/", "-I", ROOT + "/apps/dos/",
                      "-I", ROOT + "/drivers/net/", "-I", ROOT + "/kerndos/",
                      "-DDOSKPART", "-DDOS_EXTCORE"])
    dseg = u16(m.read(os88geom.winptr(m, dw.i, ui.sym) + os88geom.W_SEG, 2))
    n = u16(m.readseg(dseg, dsyms["dos_btrec"] + 6, 2))     # OS88UI_BT_N
    r = u16(m.readseg(dseg, dsyms["dos_btrec"] + 0, 2))     # OS88UI_BT_RECTS
    good = (n == 2 and r != 0)
    print("6. %-26s N=%-4d RECTS=%-6d want N=2 RECTS!=0 %s"
          % ("DOS record AIMED on paint", n, r, "PASS" if good else "FAIL"))
    ok &= good

    print("\n%s" % ("ALL PASS" if ok else "*** FAILED ***"))
    sys.exit(0 if ok else 1)
