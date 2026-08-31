#!/usr/bin/env python3
"""The Task Manager's PARTIAL repaint fills only the part (SPEC.md 28.10.1)

    make && python3 tests/tmground.py [--machine os8088_xt_vga]

`wm_draw_win` white-fills a window's content before `W_PAINT` unless
`WF_OWNBG` is set (SPEC.md 11.90.1), and this window did not set it - so every
repaint filled all 232x265 and then drew over it, on the window whose W_PAINT
is the densest in the tree (~450 ms on the target).

**THE FLAG ALONE DOES NOT FIX THAT AND THIS GATE DOES NOT CLAIM IT DOES.**
Measured with the flag set, a forced FULL repaint still flashes 49,729
transient pixels of a 61,480 px content: the fill did not go away, it changed
owner. What the flag buys is the right to be told you owe less (11.90.2's
interlock - wm_damage answers "whole" without it), and tm_clear_owed is what
spends that. So the measurement here is a PARTIAL repaint.

  FLAG      the window carries WF_OWNBG.
  STRIP     close a window covering part of it, and the transient count -
            what a person sees blink - must be bounded by the OVERLAP rather
            than by the content box. That is the whole change.
  PIXELS    ...and the ground is still COMPLETE. Cover it entirely, uncover
            it, and the glass must equal a forced full repaint: an
            under-filling W_PAINT leaves the covering window's pixels behind,
            which is exactly what the kernel's fill used to hide.

STRIP IS THE LEG THAT BITES and PIXELS is the one that keeps it honest -
filling nothing at all passes STRIP with the best score it can have.

THREE THINGS PIXELS HAS TO DO SINCE SPEC.md 28.11, and none is optional:

* **The restore has to MISS.** This window holds a raise cache now, so an
  uncover puts the banked pixels back and W_PAINT is told it owes nothing -
  `tm_update`, not `tm_clear_owed` + `tm_draw_full`. The ground fill would
  never run and the leg would pass without testing it. `spoil` forces the
  miss: `WF_SAVEU` cleared in the record while the window is covered, which
  `wm_su_ck` refuses on before it looks at anything else, and which frees
  nothing - a host poke is not `wm_saveu`, so the heap is where it was.
* **NOTHING ELSE MAY BE ON THE DESKTOP**, which is why this leg now runs last
  and opens its own cover. A forced full repaint moves somebody else's
  purgeable claim, and `PURGE nnK( n)` and the claim rows under it are on
  this page (SPEC.md 28.8). Measured with the drive window left up and
  un-zoomed rather than closed: `( 3)` against `( 2)`, 396 subpixels, in one
  run of two - all of it that count and the rows it shifts. With the Task
  Manager alone on the glass: 0, twice.
* **Both captures settle rather than tick.** Same reason one step further in:
  a purgeable claim taken and dropped again is two changes to this page, and
  a fixed number of frames lands wherever that transient happens to be.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps


def zord(m):
    n = m.read(os88sym.linear("wm_zn"), 1)[0]
    return list(m.read(os88sym.linear("wm_zord"), max(n, 1)))[:n]

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
TITLE_H = 18
WF_OWNBG = 64
fails = []


def tick(mm, card=None):
    """A settle substitute: the performance view animates, so nothing stills."""
    mm.advance(frames=110)
    mm.run()


def view(m, seg):
    return m.read((seg << 4) + dispapps.img_size("taskmgr")
                  + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]


def flags(m, slot):
    b = m.read(S("wm_wins") + slot * os88geom.WIN_SIZE + os88geom.W_FLAGS, 2)
    return b[0] | (b[1] << 8)


WSU_X1 = 0                              # SPEC.md 11.96.11.1's cache header
WF_SAVEU = 0x20


def spoil(m, slot):
    """Make this window's raise cache MISS at the uncover, freeing nothing.

    CLEARING WF_SAVEU IN THE RECORD IS THE DETERMINISTIC HALF. wm_su_ck asks
    wm_dc_ok first, so a cleared flag is a refusal whatever the claim holds -
    and a poke is not `wm_saveu`, so the claim is not freed and the PURGE
    figure this page draws does not move under the comparison. tm_promise
    re-states the flag at the END of the tm_paint this forces, so it is clear
    for exactly the one draw that matters.

    The header poke is the belt to that brace, and it is why this reads the
    cache word without trusting it (SPEC.md 11.96.18.1): a word that reads 0
    is not proof there is no claim, so the flag has to carry the guarantee.
    Answers whether the word was readable, which is information and not a
    verdict.
    """
    a = S("wm_wins") + slot * os88geom.WIN_SIZE + os88geom.W_FLAGS
    f = m.read(a, 2)
    m.write(a, bytes([f[0] & ~WF_SAVEU & 0xFF, f[1]]))
    b = m.read(S("wm_su_segs") + slot * 2, 2)
    seg = b[0] | (b[1] << 8)
    if seg:
        m.write((seg << 4) + WSU_X1, b"\xff\x7f")
    return bool(seg)


def force_repaint(m):
    """[cp_dirty] is the one flag whose only consumer is wm_paint_all, and
    WF_SAVEU is cleared so a cache cannot answer the repaint with a blit."""
    saved = []
    for w in dispcp.win_list(m, S):
        a = S("wm_wins") + w * os88geom.WIN_SIZE + os88geom.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~0x20 & 0xFF, f[1]]))
    m.write(S("cp_dirty"), b"\x01")
    return saved


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    tick(m)
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got, i = dispapps.pkg_seg(m, i), i + 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    if "Task" not in w.title:
        sys.exit("the newest package window is %r" % w.title)
    tmx = w.x + w.w - 24

    # --- FLAG --------------------------------------------------------------
    print("FLAG    : %s (%d,%d) %dx%d  flags=%04X ownbg=%s"
          % (w.title, w.x, w.y, w.w, w.h, flags(m, slot),
             bool(flags(m, slot) & WF_OWNBG)))
    if not flags(m, slot) & WF_OWNBG:
        fails.append("FLAG: the window does not carry WF_OWNBG, so the kernel "
                     "is still filling its content white before every paint")

    # The HEAP page: the densest of the three and the one PERFORMANCE.md
    # prices. Cycling needs the window frontmost, which it is.
    for _ in range(4):
        if view(m, seg) == 2:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)      # left of the scroll bar
        mo.to(*dispcorner.PARK)
        tick(m)
    if view(m, seg) != 2:
        fails.append("could not reach the heap page (view %d)" % view(m, seg))
    x1, y1, x2, y2 = w.content
    box = (x2 - x1 + 1) * (y2 - y1 + 1)

    # --- STRIP -------------------------------------------------------------
    # FIRST, because it ends with the drive window closed and PIXELS wants an
    # empty desktop. Raise the drive window over the Task Manager, then CLOSE
    # it: the vacated rect is the damage, and the Task Manager owes only its
    # own share of it - which is what tm_clear_owed fills. A drag would do as
    # well and brings the XOR outline and the drag cache into the capture; a
    # close is one edge.
    dwin = [x for x in os88geom.windows(m) if x.i == disk][0]
    mo.click(dwin.x + 30, dwin.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to cover it", limit=90)
    tick(m)
    ox1, oy1 = max(x1, dwin.x), max(y1, dwin.y)
    ox2, oy2 = min(x2, dwin.x + dwin.w - 1), min(y2, dwin.y + dwin.h - 1)
    overlap = max(0, ox2 - ox1 + 1) * max(0, oy2 - oy1 + 1)
    if overlap <= 0 or overlap >= box:
        fails.append("SETUP: the Disk window overlaps %d px of a %d px "
                     "content - this leg needs a PARTIAL cover"
                     % (overlap, box))

    cx, cy = os88geom.close_xy(dwin.x, dwin.y)
    mo.to(cx, cy)
    tick(m)
    m.pause()
    m.mouse(0, 0, l=True)
    m.mouse(0, 0, l=False)
    # 150 FRAMES, NOT 90: this page draws its own raise cache. Raising the
    # drive window banks one, which is a purgeable claim appearing in the very
    # table PURGE counts, so the Task Manager updates - and updating drops the
    # cache, which changes the table back and it updates once more. Two
    # element-level passes AFTER the repaint being measured, and 90 frames
    # caught the tail of them as `settled=False` in one run of two.
    r = m.flicker(frames=150)
    m.run()
    tick(m)
    flash = max((f["transient"] for f in r["per_frame"]), default=0)
    moved = [f for f in r["per_frame"] if f["changed"]]
    bound = overlap + (box // 20)               # the strip, plus slack for the
                                                # pointer and the chrome
    print("STRIP   : content %d px, overlap %d px; %d frames drew, transient "
          "peak %d (bound %d), settled=%s"
          % (box, overlap, len(moved), flash, bound, r["settled"]))
    if not r["settled"]:
        fails.append("STRIP: the capture never settled, so every count above "
                     "was measured against a moving target")
    if not moved:
        fails.append("STRIP: closing the Disk window drew NOTHING - the click "
                     "missed its close box and this leg tested nothing")
    if flash > bound:
        fails.append("STRIP: %d transient pixels against a bound of %d - the "
                     "repaint is filling more than it owes (SPEC.md 28.10.1)"
                     % (flash, bound))

    # --- PIXELS ------------------------------------------------------------
    # LAST, and with a cover this leg opens and CLOSES itself, so that at both
    # captures the Task Manager is the only window on the desktop. That is not
    # tidiness: a forced full repaint moves somebody else's purgeable claim,
    # and PURGE is on this page. Measured with the drive window still up,
    # un-zoomed rather than closed: `PURGE nnK( 3)` against `( 2)` and the
    # claim rows under it, 396 subpixels, in one run of two.
    #
    # ZOOM the cover rather than sizing one: a double-click on a title bar
    # takes the whole desktop band (SPEC.md 11.95), so the Task Manager's own
    # pixels are genuinely gone.
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    ws = [x for x in os88geom.windows(m) if x.visible and x.i != slot]
    if not ws:
        fails.append("PIXELS: nothing left to cover it with")
    else:
        c = ws[-1]
        mo.dblclick(c.x + 40, c.y + 9)
        mo.to(*dispcorner.PARK)
        tick(m)
        big = [x for x in os88geom.windows(m) if x.i == c.i][0]
        whole = (big.x <= w.x and big.y <= w.y
                 and big.x + big.w >= w.x + w.w
                 and big.y + big.h >= w.y + w.h)
        miss = spoil(m, slot)                       # ...and the restore MISSES
        mo.click(*os88geom.close_xy(big.x, big.y))  # ...and the cover GOES
        mo.to(*dispcorner.PARK)
        os88marty.settle(m, limit=120)
        left = [x for x in os88geom.windows(m) if x.visible and x.i != slot]
        if left:
            fails.append("PIXELS: the cover did not close - %r is still up, "
                         "and a forced repaint moves its claims" % (left,))
        cw, _, uncovered = m.fbuf()
        saved = force_repaint(m)
        mo.to(*dispcorner.PARK)
        os88marty.settle(m, limit=120)
        for a, f in saved:
            m.write(a, f)
        _, _, honest = m.fbuf()
        r0 = w.y * cw * 3
        r9 = min((w.y + w.h) * cw * 3, len(honest))
        diff = sum(1 for a, b in zip(uncovered[r0:r9], honest[r0:r9])
                   if a != b)
        print("PIXELS  : zoom covered it wholly=%s, cache %s - %d subpixels "
              "differ between the uncover and a forced repaint"
              % (whole, "spoiled, word %s" % ("read" if miss else "0"),
                 diff))
        if not whole:
            fails.append("PIXELS: the zoomed window did not wholly cover the "
                         "Task Manager, so its pixels were never gone")
        if diff:
            fails.append("PIXELS: %d subpixels differ - the window's own "
                         "ground fill does not cover everything the kernel's "
                         "used to" % diff)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  a partial repaint fills the strip, and the ground is complete")
