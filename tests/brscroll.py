#!/usr/bin/env python3
"""Does a one-line scroll cost one line, ANYWHERE in the document?

    make && make browsertest && python3 tests/brscroll.py [--adapter cga|herc]

BROWSER-PLAN 4.1 gives the scroll three tiers and the cheap one is a blit:
move the pixels already on the glass with OSAPI_GFX_SCROLL and letter only the
row that appeared. SPEC.md 71.10 is what went wrong with it - the magnitude
test read the OLD SCROLL POSITION instead of the delta, so the blit was
abandoned the moment the reader was more than one windowful into a page and
every scroll after that repainted the whole band. On the field machine that is
a scrolled line going from ~80 ms to over a second, and it reads as a property
of the CONTENT - the page that found it is a Reddit thread whose comments are
one long table, and the table is simply what happens to be on screen by the
time the view is a windowful down.

So the assertion is a comparison of the app against ITSELF rather than against
a constant: **one Down key costs the same at the top of a page as it does deep
in it.** No threshold to tune, nothing that has to be re-measured per adapter
or per window size, and it fails loudly on exactly the defect above - where a
fixed frame budget would have to be re-derived for every band height.

Two assertions:

1. A ONE-LINE SCROLL COSTS THE SAME AT DEPTH AS IT DOES SHALLOW. Measured in
   DISPLAYED FRAMES with PERFORMANCE.md Part 3.1's instrument, which is how
   often an eye samples the glass. Shallow is taken below [br_rows] - where
   the broken build still blitted, which is why it is the honest control.

2. THE PIXELS ARE THE SAME EITHER WAY. The same view is reached twice - once
   by single-line blits, once by the PageUp/PageDown pair, which is tier 2 in
   both directions and so repaints the band whole - and the two must agree
   pixel for pixel below the menu bar. The bar is excluded because its clock
   is on a clock of its own and the two routes are seconds apart.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731

_IBMROM = os.path.exists("tools/martypc/roms/ibm5150")
MACHINE = ({"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"} if _IBMROM
           else {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"})

MBAR = 19               # the menu bar and its rule: the clock lives up here


def say(*a):
    print(*a)
    sys.stdout.flush()


def browser_syms():
    """The package's own map, asserted to describe the shipped binary."""
    d = tempfile.mkdtemp()
    src, mp, out = (os.path.join(d, n) for n in ("b.asm", "b.map", "b.bin"))
    shutil.copy("apps/browser/browser.asm", src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/browser/", "-I", "drivers/net/",
                    "-o", out, src], check=True)
    if open(out, "rb").read() != open("build/browser.bin", "rb").read():
        sys.exit("brscroll: the mapped build is not build/browser.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def frames(m, key):
    """Displayed frames in which any pixel moved, for one keystroke.

    Injected while PAUSED so the action lands inside the capture window
    instead of racing it, and `settled` is checked - every count is measured
    against the end state, so a capture that never settled measured nothing.
    """
    m.pause()
    m.key(key)
    r = m.flicker(frames=90)
    m.run()
    os88marty.settle(m, quiet=0.6, stable=2, limit=90)
    if not r.get("settled"):
        sys.exit("brscroll: a %s capture never settled - every frame count "
                 "below would be measured against a moving target" % key)
    return r["moved_frames"]


def shot(m, adapter):
    w, h, rows = m.vram("cga" if adapter == "cga" else "herc")
    return w, h, b"".join(bytes(r) for r in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    a = ap.parse_args()

    sy, fails = browser_syms(), []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "TORTURE.HTM")
        w = dispcp.win_list(m, S)[-1]
        rec = m.read(S("wm_wins") + w * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))      # noqa: E731

        # the location bar opens FOCUSED and swallows the arrows (SPEC.md
        # 71.7), so a click anywhere else has to blur it first or every
        # keystroke below reaches a control that does not scroll
        wr = dispcp.win_rect(m, S, w)
        mo.click(wr[0] + wr[2] // 2, wr[1] + wr[3] - 12)
        os88marty.settle(m, quiet=0.6, stable=2, limit=60)

        rows, nlines = rw("br_rows"), rw("br_nlines")
        say("band %d rows, page %d lines, top %d" % (rows, nlines, rw("br_top")))
        if nlines < 3 * rows + 8:
            sys.exit("brscroll: TORTURE.HTM lays out at %d lines in a %d-row "
                     "band - too short to have a `deep` at all" % (nlines, rows))

        # --- 1: a Down key SHALLOW - above [br_rows], the one depth the
        # broken build also blitted, which is what makes it the control
        for _ in range(2):
            frames(m, "ArrowDown")
        shallow = [frames(m, "ArrowDown") for _ in range(3)]
        say("shallow (top %d): moved frames %s" % (rw("br_top"), shallow))

        # --- ...and DEEP, past two windowfuls
        while rw("br_top") < 2 * rows + 4:
            m.pause(); m.key("ArrowDown"); m.run()
            os88marty.settle(m, quiet=0.6, stable=2, limit=90)
        deep = [frames(m, "ArrowDown") for _ in range(3)]
        say("deep    (top %d): moved frames %s" % (rw("br_top"), deep))

        lo, hi = min(shallow), min(deep)
        if hi > 2 * lo:
            fails.append("a one-line scroll costs %d displayed frames at line "
                         "%d and %d at line %d - the blit is being abandoned "
                         "with depth (SPEC.md 71.10)"
                         % (hi, rw("br_top"), lo, rows))

        # --- 2: the same view, reached the other way, is the same pixels
        blit = shot(m, a.adapter)
        top = rw("br_top")
        for k in ("PageUp", "PageDown"):
            m.pause(); m.key(k); m.run()
            os88marty.settle(m, quiet=0.6, stable=2, limit=90)
        if rw("br_top") != top:
            fails.append("PageUp/PageDown did not land back on line %d (it is "
                         "%d) - the pixel check below would be comparing two "
                         "different views" % (top, rw("br_top")))
        else:
            band = shot(m, a.adapter)
            if band[:2] != blit[:2]:
                fails.append("the two captures are different sizes")
            else:
                sw = band[0]
                d = sum(1 for i in range(MBAR * sw, len(band[2]))
                        if band[2][i] != blit[2][i])
                say("blit route against band route: %d differing pixels of %d"
                    % (d, sw * (band[1] - MBAR)))
                if d:
                    fails.append("the blit route and the band route disagree "
                                 "by %d pixels at line %d" % (d, top))

    for f in fails:
        say("FAIL: " + f)
    say("brscroll: %s (%s)" % ("FAIL" if fails else "PASS", a.adapter))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
