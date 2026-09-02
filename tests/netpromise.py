#!/usr/bin/env python3
"""Telnet and the FTP server promise per DEBT (SPEC.md 70.7, 77.47)

    make && python3 tests/netpromise.py [--machine os8088_xt_vga]

Both windows have a worker that draws in the background and both had to
refuse a raise cache for it. Neither takes the browser's per-fetch answer
(SPEC.md 71.11), because for these two the "live" state is the one they are
in nearly all the time: a telnet window spends its life connected at a
prompt, and a file server spends its life running and quiet.

So the promise is the app's own debt word, which both already kept:
`te_owed` (the chrome, the scroll and the row range) and `[fd_dirty]`.
Nothing owed means the glass matches, and a cache taken then is what a
repaint would draw.

Five legs each, and the same five:

  REST     opened and settled, the window promises and claims two colours.
  TWO      ...and that second claim is TRUE - every content pixel is colour
           0 or 15. Checked rather than trusted: a wrong depth claim does not
           crash, it silently loses the colour on the next raise.
  BANKED   covered, it banks ONE plane.
  PIXELS   raised, the glass agrees with a forced full repaint.
  OWED     covered again, and a debt raised from outside the guest WITHDRAWS
           the promise and takes the cache with it.

OWED IS THE LEG THAT BITES. A build that set the flag once in its entry proc
passes REST, TWO, BANKED and PIXELS and is wrong for the whole of a session.

THE DEBT IS RAISED BY WRITING THE APP'S OWN BYTES, and that is tests/telnet.py's
established practice in this tree, stated in its own banner: everything here
is about which pixels are on the glass, none of it is about the transport, and
standing the cable up to make a host print one line would make the gate slower
than the thing it tests and no more true. [te_dr0] and [te_dr1] are exactly
what te_feed writes, and they are the INPUT to the predicate under test.

**THE FTP SERVER'S OWED LEG IS NOT HERE, AND THE REASON IS THE MACHINE.**
fd_hire is called from fd_start's SUCCESS path only, and fd_start opens with
net_find - so on MartyPC, which has no NIC of any kind (docs/TESTING.md's
entry 5), pressing Start fails at the first call and the worker is never
spawned. With no worker nothing calls fd_flush_glass, so a debt poked in from
here is never flushed and the withdrawal cannot fire. That is not the code
being untestable, it is this machine having no way to make an FTP server do
anything: the leg lives in tests/ftpd.py, which boots QEMU with ETHER.DRV and
drives a real client, and reads guest memory through ethernet.py's pmemsave.
The four legs above are exactly the ones that do not need a wire, and the
fifth belongs in tests/ftpd.py.
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
MOVE_Y = 240                            # see run(): where the app is dropped
BLACK, WHITE = b"\x00\x00\x00", b"\xff\xff\xff"
fails = []


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def cache(m, i):
    b = m.read(S("wm_su_segs") + i * 2, 2)
    seg = b[0] | (b[1] << 8)
    if not seg:
        return 0, None, 0, 0
    h = m.readseg(seg, 0, 16)
    x1, x2 = h[0] | (h[1] << 8), h[4] | (h[5] << 8)
    y1, y2 = h[2] | (h[3] << 8), h[6] | (h[7] << 8)
    bpr, rows = (x2 // 8) - (x1 // 8) + 1, y2 - y1 + 1
    pw = h[12] | (h[13] << 8)
    return seg, pw, 14 + (bpr + 2) * rows, 14 + (bpr + 2) * rows * 4


def bss(m, seg, app, name):
    return (seg << 4) + dispapps.img_size(app) + dispapps.bss_off(app, name)


def raise_win(m, mo, x, y, i, what):
    mo.click(x, y)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == i,
                    "%s to come to the front" % what, limit=90)


def run(m, mo, app, pkgfile, title, owe):
    """One app, five legs. `owe(m, seg)` raises a debt, or None to skip it."""
    tag = title.upper()
    disk = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, pkgfile)
    os88marty.settle(m)
    # THE NEWEST package window, not the Nth. run() closes each app before the
    # next opens, so counting the ones this gate has seen asks pkg_seg for an
    # index that is no longer there and reads as "FTPD.O88 did not open".
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got = dispapps.pkg_seg(m, i)
        i += 1
    if got is None:
        sys.exit("%s: %s opened no package window - %r"
                 % (tag, pkgfile, os88geom.windows(m)))
    slot, seg = got
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    if title.lower() not in w.title.lower():
        sys.exit("%s: the newest package window is %r, not %s"
                 % (tag, w.title, title))

    # MOVE IT DOWN, and this is not cosmetic. Both windows open at (39,40) and
    # are wide enough to swallow the drive window whole - 530x190 and 402x176
    # against a Disk at (103,80) 322x200 - so a click on the drive window's
    # TITLE BAR lands on the app instead and raises the wrong one. Nothing
    # then fails: the cache stays banked and every reading below is of a
    # window that was never covered. Dropped at y = MOVE_Y the two overlap by
    # a band, which is all wm_su_take needs, and each title bar is clear of
    # the other rect.
    mo.drag(w.x + 40, w.y + 9, w.x + 40, MOVE_Y + 9)
    os88marty.settle(m)
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    if w.y != MOVE_Y:
        fails.append("%s: the window did not move to y=%d (it is at %d) - the "
                     "legs below would test the wrong z-order" % (tag, MOVE_Y, w.y))
    if wy + 9 >= w.y:
        fails.append("%s: the drive window's title bar at y=%d is not clear of "
                     "the app at y=%d" % (tag, wy + 9, w.y))

    # --- REST --------------------------------------------------------------
    print("%-7s REST   : %s (%d,%d) %dx%d saveu=%s 1bpp=%s"
          % (tag, w.title, w.x, w.y, w.w, w.h, w.promises, w.mono))
    if not w.promises:
        fails.append("%s REST: settled and it does not carry WF_SAVEU - "
                     "nothing is owed, so the glass matches" % tag)
    if not w.mono:
        fails.append("%s REST: it does not carry WF_1BPP" % tag)

    # --- TWO ---------------------------------------------------------------
    mo.to(*dispcorner.PARK)                 # the arrow is drawn INTO the
                                            # framebuffer (SPEC.md 7.1)
    os88marty.settle(m)
    fw, fh, fb = m.fbuf()
    x1, y1, x2, y2 = w.content
    odd = {}
    for y in range(y1, min(y2, fh - 1) + 1):
        for x in range(x1, min(x2, fw - 1) + 1):
            p = fb[(y * fw + x) * 3:(y * fw + x) * 3 + 3]
            if p not in (BLACK, WHITE):
                odd[p.hex()] = odd.get(p.hex(), 0) + 1
    print("%-7s TWO    : content %s - %d pixels neither 0 nor 15 %s"
          % (tag, (x1, y1, x2, y2), sum(odd.values()), sorted(odd.items())[:3]))
    if odd:
        fails.append("%s TWO: %d content pixels are neither colour 0 nor 15 - "
                     "the WF_1BPP claim is not true" % (tag, sum(odd.values())))

    # --- BANKED ------------------------------------------------------------
    raise_win(m, mo, wx + 30, wy + 9, disk, "the drive window")
    sg, pw, one, four = cache(m, slot)
    print("%-7s BANKED : claim %04X, %d planes, %d bytes (%d at four, ceiling "
          "%d)" % (tag, sg, pw or 0, one, four, SU_KB))
    if not sg:
        fails.append("%s BANKED: covered at rest and no cache was taken" % tag)
    elif MACHINE.endswith("vga") and pw != 1:
        fails.append("%s BANKED: the claim is %d planes and the window claims "
                     "two colours - wanted 1" % (tag, pw))

    # --- PIXELS ------------------------------------------------------------
    raise_win(m, mo, w.x + 40, w.y + 9, slot, title)
    if cache(m, slot)[0]:
        fails.append("%s PIXELS: the raise did not spend the cache" % tag)
    os88marty.settle(m)
    cw, _, cached = m.fbuf()
    dispcorner.repaint(m, mo, None)
    _, _, honest = m.fbuf()
    skip = 20 * cw * 3                  # below the menu bar: SPEC.md 37's clock
    diff = sum(1 for a, b in zip(cached[skip:], honest[skip:]) if a != b)
    print("%-7s PIXELS : %d subpixels differ between the cached raise and the "
          "honest repaint" % (tag, diff))
    if diff:
        fails.append("%s PIXELS: %d subpixels differ between the cached raise "
                     "and the honest repaint" % (tag, diff))

    # --- OWED --------------------------------------------------------------
    if owe is None:
        print("%-7s OWED   : SKIP - see the banner: this app has NO WORKER on "
              "a machine with no NIC, so nothing flushes and the debt cannot "
              "be spent from here" % tag)
        return
    raise_win(m, mo, wx + 30, wy + 9, disk, "the drive window")
    if not cache(m, slot)[0]:
        fails.append("%s OWED: no cache to lose - the setup for this leg did "
                     "not happen" % tag)
    owe(m, seg)                         # ...the app's own debt bytes
    os88marty.until(m, lambda mm: not cache(mm, slot)[0]
                    and not [x for x in os88geom.windows(mm)
                             if x.i == slot][0].promises,
                    "%s to withdraw the promise" % title, limit=60)
    w2 = [x for x in os88geom.windows(m) if x.i == slot][0]
    print("%-7s OWED   : debt raised -> saveu=%s cache=%04X"
          % (tag, w2.promises, cache(m, slot)[0]))
    if w2.promises:
        fails.append("%s OWED: content is owed and the promise stands" % tag)
    if cache(m, slot)[0]:
        fails.append("%s OWED: the withdrawal left the cache banked" % tag)

    close(m, mo, w, slot, title)


def close(m, mo, w, slot, title):
    """...so the next app opens over a clean desktop."""
    cx, cy = os88geom.close_xy(w.x, w.y)
    raise_win(m, mo, w.x + 40, w.y + 9, slot, title)
    mo.click(cx, cy)
    mo.to(*dispcorner.PARK)
    os88marty.settle(m)


def owe_telnet(m, seg):
    """te_feed's own writes: mark row 0 dirty."""
    m.write(bss(m, seg, "telnet", "te_dr0"), b"\x00\x00")
    m.write(bss(m, seg, "telnet", "te_dr1"), b"\x00\x00")


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)
    # THE LAYOUT NEEDS A TALL SCREEN, and saying so beats failing at the drag.
    # run() drops the app at MOVE_Y so the drive window's title bar is clear
    # of it; on a 640x200 CGA the desktop band is 155 rows and there is no
    # second row of window to be had - the mouse simply cannot reach y=249 and
    # the error is about a clamp, which reads like a broken harness. Nothing
    # is lost by refusing: WSU_PW is 1 by ADAPTER on both 1bpp screens, so the
    # depth claim this gate checks is a VGA question, and the promise itself
    # is adapter-independent.
    vh = int.from_bytes(m.read(S("vid_h"), 2), "little")
    if vh < MOVE_Y + 200:
        sys.exit("netpromise: this desktop is %d rows and the layout needs "
                 "%d - run it on a VGA machine (see the comment in run())"
                 % (vh, MOVE_Y + 200))
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    run(m, mo, "telnet", "TELNET.O88", "Telnet", owe_telnet)
    run(m, mo, "ftpd", "FTPD.O88", "FTP", None)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  both windows promise while their glass matches, and withdraw "
      "when it does not")
