#!/usr/bin/env python3
"""Fractal promises when the frame lands, and takes it back when it moves
   (SPEC.md 40.4)

    make bench 2>/dev/null; make && python3 tests/frpromise.py

While it renders, this app is SPEC.md 11.96.1's disqualifier in its purest
form: `fr_emit_body` makes only the PAINTING conditional on visibility and
caches the row and steps the state machine either way, so a buried fractal
goes on changing its content for the minute or two a frame takes. At pass 3
the worker takes `.idle` and touches nothing at all - no lock, no clip, no
pixel - which is the flattest resting state in the tree.

  ATREST   the frame lands (pass 3) and the promise is granted.
  BANKED   covering it there banks a cache, four planes deep - the canvas is
           sixteen colours, so there is no WF_1BPP claim and the four-plane
           figure has to fit wm_su_kb's ceiling on its own.
  PIXELS   raised again, the glass agrees with a picture rendered from
           scratch. The comparison waits for pass 3 a second time, because
           the forced repaint puts fr_redraw's replay in front of it.
  AGAIN    a view change restarts the render and withdraws the promise...
  LIVE     ...and covering it THERE banks no cache.

AGAIN AND LIVE ARE THE HALF THAT BITE. A build that simply set
OSAPI_SAVEU_ON in the package header would pass ATREST, BANKED and PIXELS
and be wrong for the whole of a render.

THE ORDER IS DELIBERATE AND THE FIRST ONE WAS NOT. This looked at the live
state FIRST, on the reasoning that a freshly opened fractal is rendering -
and every run reported the frame already complete, so the two legs that
matter passed on nothing. `dispcp.open_named` ends in `settle`, and settling
means waiting for a still screen: on this window that is waiting out the
whole render. The live state has to be MADE, by clicking the canvas, and read
without settling afterwards.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
SU_KB = 0xFC00
TITLE_H = 18
DONE = 3                                # [fr_pass] == 3: the frame is complete
fails = []


def frwin(m):
    w = [x for x in os88geom.windows(m) if "Fractal" in x.title]
    return w[-1] if w else None


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def cache(m, i):
    b = m.read(S("wm_su_segs") + i * 2, 2)
    seg = b[0] | (b[1] << 8)
    if not seg:
        return 0, None, 0
    h = m.readseg(seg, 0, 16)
    x1, x2 = h[0] | (h[1] << 8), h[4] | (h[5] << 8)
    y1, y2 = h[2] | (h[3] << 8), h[6] | (h[7] << 8)
    bpr = (x2 // 8) - (x1 // 8) + 1
    return seg, h[12] | (h[13] << 8), 14 + (bpr + 2) * (y2 - y1 + 1) * 4


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)               # a whole frame is guest minutes
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    wx, wy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "FRACTAL.O88")
    m.advance(frames=200)
    m.run()
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        sys.exit("frpromise: fractal did not open")
    slot, seg = got
    fr = frwin(m)
    if fr is None:
        sys.exit("frpromise: no Fractal window - %r" % os88geom.windows(m))

    def st():
        return dispapps.words(m, seg, "fractal", ["fr_pass", "fr_prog"])

    # --- ATREST ------------------------------------------------------------
    # dispcp.open_named ended in a settle, and a settle on this window waits
    # out the render - so the frame is already complete here. That is why
    # this leg is first and the live one is made by hand below.
    t = os88marty.until(
        m, lambda mm: dispapps.words(mm, seg, "fractal",
                                     ["fr_pass"])["fr_pass"] >= DONE,
        "the frame to land (fr_pass 3)", poll=2.0, limit=900)
    w = st()
    print("ATREST  : %s (%d,%d) %dx%d  pass=%d prog=%d saveu=%s 1bpp=%s "
          "(+%.0fs)" % (fr.title, fr.x, fr.y, fr.w, fr.h, w["fr_pass"],
                        w["fr_prog"], frwin(m).promises, frwin(m).mono, t))
    if not frwin(m).promises:
        fails.append("ATREST: the frame is complete and the window still does "
                     "not carry WF_SAVEU - fr_emit_body's grant never fired")
    if frwin(m).mono:
        fails.append("ATREST: it claims WF_1BPP, and the canvas is sixteen "
                     "colours - the claim is not true")

    # --- BANKED ------------------------------------------------------------
    os88marty.settle(m)                 # a finished fractal IS still
    cw, _, _ = m.fbuf()
    dk = [x for x in os88geom.windows(m) if x.visible and x.i == disk][0]
    mo.click(dk.x + 30, dk.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to cover it", limit=90)
    sg, pw, four = cache(m, fr.i)
    print("BANKED  : claim %04X, %d planes, %d bytes at four (ceiling %d)"
          % (sg, pw or 0, four, SU_KB))
    if not sg:
        fails.append("BANKED: covered at rest and no cache was taken")
    elif MACHINE.endswith("vga"):
        if pw != 4:
            fails.append("BANKED: the claim is %d planes and the canvas is "
                         "sixteen colours - wanted 4" % pw)
        if four > SU_KB:
            fails.append("BANKED: four planes is %d bytes, past wm_su_kb's %d "
                         "ceiling - this window needs a depth claim too"
                         % (four, SU_KB))

    # --- PIXELS ------------------------------------------------------------
    mo.click(fr.x + 40, fr.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == fr.i,
                    "the fractal to be raised off the cache", limit=90)
    if cache(m, fr.i)[0]:
        fails.append("PIXELS: the raise did not spend the cache")
    os88marty.settle(m)
    _, _, raised = m.fbuf()
    dispcorner.repaint(m, mo, None)     # forces W_PAINT -> fr_redraw, which
                                        # withdraws and replays: wait it out
    os88marty.until(
        m, lambda mm: dispapps.words(mm, seg, "fractal",
                                     ["fr_pass"])["fr_pass"] >= DONE,
        "the forced repaint to finish replaying", poll=2.0, limit=900)
    os88marty.settle(m)
    _, _, honest = m.fbuf()
    skip = 20 * cw * 3
    diff = sum(1 for a, b in zip(raised[skip:], honest[skip:]) if a != b)
    print("PIXELS  : %d subpixels differ between the cached raise and the "
          "re-rendered picture" % diff)
    if diff:
        fails.append("PIXELS: %d subpixels differ between the cached raise "
                     "and the re-rendered picture" % diff)

    # --- AGAIN -------------------------------------------------------------
    # A click on the canvas recentres and zooms: fr_kick, pass back to 0.
    # NO SETTLE FROM HERE ON - the canvas is a band a row arriving, and
    # waiting for it to stop is waiting for the whole render.
    mo.click(fr.x + fr.w // 2, fr.y + TITLE_H + 60)
    mo.to(*dispcorner.PARK)
    os88marty.until(
        m, lambda mm: dispapps.words(mm, seg, "fractal",
                                     ["fr_pass"])["fr_pass"] < DONE,
        "the click to restart the render", limit=120)
    print("AGAIN   : pass=%d saveu=%s cache=%04X"
          % (st()["fr_pass"], frwin(m).promises, cache(m, fr.i)[0]))
    if frwin(m).promises:
        fails.append("AGAIN: a view change restarted the render and the "
                     "promise stands - fr_kick's withdrawal never fired")

    # --- LIVE ----------------------------------------------------------------
    mo.click(dk.x + 30, dk.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to cover the render", limit=90)
    w = st()                            # pass AND the cache in one look: if
    sg = cache(m, fr.i)[0]              # the frame landed while this waited
    print("LIVE    : pass=%d prog=%d saveu=%s cache=%04X"
          % (w["fr_pass"], w["fr_prog"], frwin(m).promises, sg))
    if w["fr_pass"] >= DONE:
        fails.append("LIVE: the frame finished while the cover was going in - "
                     "this leg tested nothing (make the render longer, or "
                     "raise the zoom)")
    elif sg:
        fails.append("LIVE: a cache was banked at %04X at pass %d - a buried "
                     "fractal goes on emitting rows (SPEC.md 40.2/11.96.1)"
                     % (sg, w["fr_pass"]))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  the promise follows the frame")
