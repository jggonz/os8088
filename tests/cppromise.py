#!/usr/bin/env python3
"""The Control Panel promises per PAGE, and the clock is the one that cannot
   (SPEC.md 31.12)

    make && python3 tests/cppromise.py [--machine os8088_xt_vga]

SPEC.md 11.96.1's question - "does your content change while you are not
drawing?" - has no single answer for this window. On five of its six pages
nothing moves without a click, and a click cannot reach a window that is not
frontmost; on Date & Time, `ui_task` calls `cp_tick` once a second and
`cp_tick_x` SKIPS DRAWING when `wm_obscured` says so, which is the
disqualifier exactly - the clock advances and the glass does not.

So `cp_promise` answers it from `[cp_sel]`, at the one choke point every path
that can change the answer goes through (`cp_page`).

  PROMISE  on Scheduler the panel carries WF_SAVEU.
  BANKED   ...and covering it banks a cache, four planes deep - the panel
           draws in CDGRAY (SPEC.md 47), so there is no 1bpp claim here and
           the four-plane figure has to fit `wm_su_kb`'s ceiling on its own.
  PIXELS   raised again, the glass agrees with a forced full repaint.
  CLOCK    switch to Date & Time and the promise is WITHDRAWN, the cache
           goes with it, and covering it banks nothing.
  BACK     switch back and it promises again.

CLOCK IS THE HALF THAT BITES. Take `cp_promise` out and PROMISE/BANKED/PIXELS
all still pass - the panel simply never promised anything before this - so a
gate that stopped at BANKED would be testing the kernel, not the panel.

`settle` CANNOT BE USED ON THE DATE & TIME PAGE: the seconds field redraws
once a guest second, so the screen is never still and `settle` waits out its
whole limit and then blames the boot. Every wait past the page switch is an
`until` on state instead.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
CP_ITIME, CP_ISCHED = 1, 0
CP_IX, CP_I0Y, CP_IROWH, TITLE_H = 6, 6, 14, 18
SU_KB = 0xFC00                          # wm_su_kb's ceiling: SPEC.md 11.96
fails = []


def cpwin(m):
    w = [x for x in os88geom.windows(m) if "Control" in x.title]
    return w[-1] if w else None


def zord(m):
    """The z-order back to front - wm_zord, [0] backmost, [wm_zn-1] front."""
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def cache(m, i):
    """(segment, depth in planes, four-plane size) of window i's raise cache."""
    b = m.read(S("wm_su_segs") + i * 2, 2)
    seg = b[0] | (b[1] << 8)
    if not seg:
        return 0, None, 0
    h = m.readseg(seg, 0, 16)
    x1, x2 = h[0] | (h[1] << 8), h[4] | (h[5] << 8)
    y1, y2 = h[2] | (h[3] << 8), h[6] | (h[7] << 8)
    bpr = (x2 // 8) - (x1 // 8) + 1
    return seg, h[12] | (h[13] << 8), 14 + (bpr + 2) * (y2 - y1 + 1) * 4


def page(m, mo, cp, rec):
    """Click the left list's row for RECORD `rec` and wait for the switch.

    dispcp.open_panel does this and then settles, which is the one thing that
    cannot happen on the way to Date & Time.
    """
    hide = m.read(S("cp_hide"), 1)[0]
    row = sum(1 for r in range(rec) if not (hide & (1 << r)))
    mo.click(cp.x + 1 + CP_IX + 30,
             cp.y + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: mm.read(S("cp_sel"), 1)[0] == rec,
                    "the panel to show record %d" % rec, limit=90)


def raise_win(m, mo, x, y, i):
    mo.click(x, y)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == i,
                    "window %d to come to the front" % i, limit=90)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)               # this gate drives for guest minutes
    dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
    cp = cpwin(m)
    if cp is None:
        sys.exit("the Control Panel did not open")
    # THE COVER IS THE B: DRIVE WINDOW, and the panel is raised back by its
    # title bar RIGHT of the drive window's right edge - they overlap almost
    # entirely, so a click at the panel's usual title-bar x lands on the
    # drive window instead and raises the wrong one. Nothing then fails: the
    # cache stays banked and every reading below is of a window that was
    # never raised.
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    os88marty.settle(m)
    dk = [w for w in os88geom.windows(m) if w.visible and w.i != cp.i][-1]
    tx = cp.x + cp.w - 24
    if tx <= dk.x + dk.w - 1:
        fails.append("SETUP: the drive window's right edge (%d) covers the "
                     "panel's title bar out to %d - nothing below can raise "
                     "the panel" % (dk.x + dk.w - 1, tx))

    # --- PROMISE -----------------------------------------------------------
    cp = cpwin(m)
    print("PROMISE : %s (%d,%d) %dx%d sel=%d saveu=%s 1bpp=%s"
          % (cp.title, cp.x, cp.y, cp.w, cp.h, m.read(S("cp_sel"), 1)[0],
             cp.promises, cp.mono))
    if not cp.promises:
        fails.append("PROMISE: the panel does not carry WF_SAVEU on the "
                     "Scheduler page")
    if cp.mono:
        fails.append("PROMISE: the panel claims WF_1BPP, and it draws in "
                     "CDGRAY (SPEC.md 47) - the claim is not true")

    # --- BANKED ------------------------------------------------------------
    seg, pw, four = cache(m, cp.i)
    print("BANKED  : covered -> claim %04X, %d planes, %d bytes at four "
          "(ceiling %d)" % (seg, pw or 0, four, SU_KB))
    if not seg:
        fails.append("BANKED: the panel was covered and got no cache at all")
    elif MACHINE.endswith("vga"):
        if pw != 4:
            fails.append("BANKED: the claim is %d planes and the panel makes "
                         "no 1bpp claim - wanted 4" % pw)
        if four > SU_KB:
            fails.append("BANKED: four planes is %d bytes, past wm_su_kb's "
                         "%d ceiling - this window needs a depth claim, not "
                         "just a promise" % (four, SU_KB))

    # --- PIXELS ------------------------------------------------------------
    raise_win(m, mo, tx, cp.y + 9, cp.i)
    if cache(m, cp.i)[0]:
        fails.append("PIXELS: the raise did not spend the cache - the click "
                     "did not reach the panel's title bar")
    os88marty.settle(m)
    cw, _, cached = m.fbuf()
    dispcorner.repaint(m, mo, None)
    _, _, honest = m.fbuf()
    skip = 20 * cw * 3                  # below the menu bar: SPEC.md 37's clock
    diff = sum(1 for a, b in zip(cached[skip:], honest[skip:]) if a != b)
    print("PIXELS  : %d subpixels differ between the cached raise and the "
          "honest repaint" % diff)
    if diff:
        fails.append("PIXELS: %d subpixels differ between the cached raise "
                     "and the honest repaint" % diff)

    # --- CLOCK -------------------------------------------------------------
    page(m, mo, cp, CP_ITIME)
    cp = cpwin(m)
    seg = cache(m, cp.i)[0]
    print("CLOCK   : sel=%d saveu=%s cache=%04X"
          % (m.read(S("cp_sel"), 1)[0], cp.promises, seg))
    if cp.promises:
        fails.append("CLOCK: the panel still carries WF_SAVEU on Date & Time, "
                     "whose seconds field redraws once a second and SKIPS the "
                     "draw when covered (SPEC.md 31.5.1/11.96.1)")
    if seg:
        fails.append("CLOCK: the withdrawal left the cache banked at %04X - "
                     "wm_saveu's clear calls wm_su_drop" % seg)
    raise_win(m, mo, dk.x + 30, dk.y + 9, dk.i)
    seg = cache(m, cp.i)[0]
    print("        : covered on Date & Time -> cache=%04X" % seg)
    if seg:
        fails.append("CLOCK: covering the panel on Date & Time banked a cache "
                     "at %04X - a raise would show the wrong time" % seg)

    # --- BACK --------------------------------------------------------------
    raise_win(m, mo, tx, cp.y + 9, cp.i)
    page(m, mo, cp, CP_ISCHED)
    cp = cpwin(m)
    if not cp.promises:
        fails.append("BACK: the panel did not promise again on the Scheduler "
                     "page - the withdrawal is one-way")
    raise_win(m, mo, dk.x + 30, dk.y + 9, dk.i)
    seg, pw, _ = cache(m, cp.i)
    print("BACK    : sel=%d saveu=%s  covered -> claim %04X, %d planes"
          % (m.read(S("cp_sel"), 1)[0], cp.promises, seg, pw or 0))
    if not seg:
        fails.append("BACK: back on Scheduler and covered, the panel banked "
                     "no cache")

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  the panel promises per page, and the clock page withdraws it")
