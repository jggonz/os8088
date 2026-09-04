#!/usr/bin/env python3
"""WHICH MONITOR DOES A FULLSCREEN BRACKET LAND ON? (SPEC.md 53.7.1)

    make && python3 tests/dispfsx.py                    # window on the PRIMARY
    make && python3 tests/dispfsx.py --far              # ...on the second
    make && python3 tests/dispfsx.py --far --noxt       # ...as a SAME-MODE one
    make && python3 tests/dispfsx.py --app paint --far

Reported from the field: "switching Tracker to full screen then back corrupts
the other screen."

TWO DIFFERENT QUESTIONS ARE ASKED HERE, and the first four runs of this
script answered only the second - which passed, and is not the defect.

  1. WHILE THE BRACKET IS UP, which card changed? The bracket owns ONE
     display (SPEC.md 53.7.1) - its own must change a lot, and the other
     must not change at all.
  2. AFTER the round trip, does either card differ from a FRESH FULL
     REPAINT? "Corrupt" means precisely that, and comparing against the
     capture taken BEFORE the trip cannot say it - the desktop is live
     state (a clock in the bar, the app's own window content) that
     legitimately differs between two captures seconds apart.
     tests/dispmcfs.py established that shape.

Question 2 is clean on every arrangement and always was: SPEC.md 53.6's exit
wm_paint_all repaints the world. Question 1 is what caught the defect - a
same-mode bracket (53.7) does not collapse a two-display desktop (39.18.3),
so (0,0) is the PRIMARY, and an app reading "fullscreen" as (0,0) plus
OSAPI_VIDEO's size drew its whole face onto the monitor it is NOT on.

BOTH SAME-MODE CONSUMERS ARE DRIVABLE (`--app`), because the fix is one
kernel slot and two apps that had the identical line. Tracker is also the
only shipped package that can be EITHER kind of bracket: XT mode on (a
tier-0 machine's default) makes `F` a mode-setting FSXM_TEXT80 bracket, and
`--noxt` presses X first to get the same-mode one.

Three things are captured at every step, because "corrupts the other screen"
is a statement about the card the user was NOT looking at and a screenshot of
the other one cannot answer it: both cards' rasterised frames AND their
raster dimensions, and the vid_ctx records - the last two because a card left
in the wrong MODE, or a record left describing the wrong geometry, is a state
a repaint cannot fix, so question 2 would read 0.

The LIVE video block is captured too and deliberately NOT asserted on, which
cost a false failure to learn: [vid_cur] follows the DRAWING, so which
display's geometry is loaded at any given moment is not an invariant at all -
the windowed capture legitimately held the Hercules' (seg B000, stride 90,
720x348) because that is where the app's window is, and the one after the
trip held the VGA's. It is only a defect when the SAME display is current at
both ends and the geometry moved anyway, and that is what is tested.
"""
import argparse
import subprocess
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

S = os88sym.linear
TITLE_H = 18
MBAR_H = 20
KINDS = {0: "VGA", 1: "HERC", 2: "CGA"}

# A DISK OF ITS OWN PER APP, so the package is row 0 of B:'s root and there is
# no navigation in the run at all. Built here rather than by the Makefile,
# because it is two lines and neither image ships:
#
#   python3 tools/os88disk.py -o /tmp/fsx-tracker.img --size 360 \
#           build/tracker.o88
#
# The one-file disk is a CONVENIENCE now and no longer a requirement: on the
# shipped apps disk TRACKER.O88 is the last row of a folder that does not fit
# a 640x200 Disk window, and dispcp.open_named scrolls to it (it asks the
# guest where the list is and where the entry is). What the small disk still
# buys is speed - one mount, one row - so it stays.
APPS = {
    # name:      (the .o88 to put on the disk, its FILE NAME, the key that
    #             takes the screen)
    "tracker":   ("build/tracker.o88", "TRACKER.O88", "KeyF"),
    "paint":     ("build/paint.o88", "PAINT.O88", "KeyF"),
}                        # SPEC.md 42.7: the bare letter, while no text tool
                         # is typing


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def lit(d):
    return sum(1 for i in range(0, len(d), 3) if d[i] or d[i + 1] or d[i + 2])


def state(m, label, cards, win=None):
    """PAUSE FIRST - the worker draws continuously and vid_ctx_act publishes
    [vid_ox]/[vid_oy] with two stores, so a running read can show a pairing
    that never existed (tests/dispmcfs.py's own trap).

    THE RASTER'S OWN DIMENSIONS AND THE ctx RECORDS COME BACK TOO, and that is
    not padding: the round-trip assertion below is "does this differ from a
    fresh repaint", and a card left in the WRONG MODE, or a display record
    left describing the wrong geometry, is a state a repaint cannot fix -
    both captures are then equally wrong and the diff reads 0. Those two are
    the whole blind spot of a repaint comparison, so they are measured
    directly."""
    m.pause()
    fb = {c: m.fbuf(card=c) for c in cards}
    out = {"fb": {c: fb[c][2] for c in cards},
           "geom": {c: (fb[c][0], fb[c][1]) for c in cards},
           "ctx": bytes(m.read(S("vid_ctx"), 2 * VID_CTX_SZ)),
           "cur": m.read(S("vid_cur"), 1)[0],
           "live": bytes(m.read(S("vid_seg"), 36))}
    print("   %-24s ndisp=%d cur=%d kind=%-4s w=%d h=%d  fsx_cur=0x%02X"
          % (label, m.read(S("vid_ndisp"), 1)[0], m.read(S("vid_cur"), 1)[0],
             KINDS.get(m.read(S("vid_kind"), 1)[0], "?"),
             u16(m.read(S("vid_w"), 2)), u16(m.read(S("vid_h"), 2)),
             m.read(S("fsx_cur"), 1)[0]))
    print("   %-24s raster: %s   lit: %s"
          % ("", {("card%d" % c): "%dx%d" % out["geom"][c] for c in cards},
             {("card%d" % c): lit(fb[c][2]) for c in cards}))
    if win is not None:
        print("   %-24s win %r" % ("", dispcp.win_rect(m, S, win)))
    m.run()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="tracker", choices=sorted(APPS),
                    help="which same-mode bracket to drive (SPEC.md 53.7.1)")
    ap.add_argument("--far", action="store_true",
                    help="drag the app onto the SECOND display first")
    ap.add_argument("--mode", default="right", choices=("right", "below"))
    ap.add_argument("--apps", default=None,
                    help="an apps floppy to use instead of the one-package "
                         "disk this builds")
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--noxt", action="store_true",
                    help="tracker only: press X first, so `F` is a SAME-MODE "
                         "bracket (SPEC.md 53.7) rather than FSXM_TEXT80")
    ap.add_argument("--primary", default=None, choices=("vga", "herc", "cga"),
                    help="make this adapter the primary first (SPEC.md "
                         "39.19.2's other two arrangements)")
    a = ap.parse_args()
    o88, pkg, fskey = APPS[a.app]
    apps = a.apps
    if apps is None:
        apps = "/tmp/fsx-%s.img" % a.app
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", apps,
                        "--size", "360", o88], check=True,
                       stdout=subprocess.DEVNULL)
    if a.noxt and a.app != "tracker":
        sys.exit("--noxt is tracker's XT-mode toggle (SPEC.md 45.9); %s has "
                 "only the one bracket" % a.app)

    with os88marty.launch("build/os8088-360.img", apps=apps,
                          machine=a.machine, boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        # A ONE-CARD MACHINE IS A LEGAL RUN, and it is the REGRESSION leg:
        # SPEC.md 53.7.1's whole claim is that fsx_surf answers (0,0,w,h)
        # there, so the two apps behave exactly as they did before it existed.
        # Nothing to extend, no second card to hold still - only "did the
        # bracket fill the screen and come home clean".
        one = len(cards) < 2
        if not one:
            dispcp.open_panel(m, mo, S, os88marty.settle)
            dispcp.set_mode(m, mo, S, os88marty.settle, a.mode)
        if a.primary and not one:
            avail = m.read(S("vid_avail"), 1)[0]
            kind = [k for k, v in dispcp.KIND_CARD.items()
                    if v == dispcp.PRIMARY_CARD[a.primary]][0]
            want = dispcp.PRIMARY_CARD[a.primary]
            card = [i for i, c in cards.items() if c["type"] == want][0]
            dispcp.set_primary(m, mo, S, os88marty.settle,
                               dispcp.adapter_row(avail, kind), card=card)
        # WHICH CARD IS WHICH IS ASKED OF THE KERNEL, not assumed: the point
        # of --primary is that it moves, and a hard-coded "the VGA is card 0"
        # then compares the wrong pair silently.
        pri = [i for i, c in cards.items()
               if c["type"] == dispcp.KIND_CARD[m.read(S("vid_kind"), 1)[0]]][0]
        sec = pri if one else [i for i in cards if i != pri][0]
        both = (pri,) if one else (pri, sec)
        if one:
            print("ONE display: card %d (%s) - the regression leg"
                  % (pri, cards[pri]["type"]))
            seam = (0, 0)
        else:
            print("primary is card %d (%s), second is card %d (%s)"
                  % (pri, cards[pri]["type"], sec, cards[sec]["type"]))
            dispcp.close_panel(m, mo, S, os88marty.settle, card=pri)
            ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
            seam = (u16(ctx, VID_CTX_SZ + VID_CTX_VX),
                    u16(ctx, VID_CTX_SZ + VID_CTX_VY))
            print("extended %s; the SECOND display sits at virtual %r"
                  % (a.mode, seam))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, pkg,
                          card=pri)
        time.sleep(6)
        t = [w for w in dispcp.win_list(m, S) if w != disk]
        if not t:
            sys.exit("%s did not launch - is %s on %s?"
                     % (a.app, pkg, apps))
        t = t[-1]

        wx, wy, ww, wh = dispcp.win_rect(m, S, t)
        if a.far and one:
            sys.exit("--far needs a second display")
        if a.far:
            mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                    seam[0] + 340, seam[1] + TITLE_H // 2)
            os88marty.settle(m, card=sec)
            wx, wy, ww, wh = dispcp.win_rect(m, S, t)
        print("%s x %d..%d, centre %d (%s of the seam)"
              % (a.app, wx, wx + ww - 1, wx + ww // 2,
                 "right" if wx + ww // 2 >= seam[0] else "left"))

        # WHICH OF THE TWO BRACKETS THIS IS, and it is worth being explicit
        # rather than inferring it from the CPU tier: XT mode on (a tier-0
        # machine's default) makes `F` a MODE-SETTING bracket - FSXM_TEXT80,
        # so SPEC.md 39.18's collapse runs - and XT mode off makes it a
        # SAME-MODE one (SPEC.md 53.7), which by 39.18.3 does NOT collapse and
        # so draws the FT2 screen in VIRTUAL coordinates across a desktop that
        # is now two monitors wide. They are different code paths and only one
        # of them is the field machine's default, so both are drivable.
        if a.noxt:
            mo.to(wx + ww // 2, wy + wh // 2)
            os88marty.settle(m, card=pri if not a.far else sec)
            m.key("KeyX")                   # SPEC.md 45.9's toggle
            time.sleep(3)
            os88marty.settle(m, card=pri if not a.far else sec)
        m.pause()
        if a.app == "tracker":
            print("XT mode is %s: `F` takes the %s path"
                  % (("ON", "FSXM_TEXT80 mode-setting") if not a.noxt else
                     ("OFF", "same-mode graphics")))
        else:
            print("%s sets no mode at all: the bracket is SPEC.md 53.7's "
                  "same-mode one" % a.app)
        m.run()

        # THE ARROW GOES TO THE SAME PLACE FOR EVERY CAPTURE and that is the
        # whole requirement - identical pixels in the pair being compared, so
        # it cancels out. It does NOT have to be over empty desktop, and
        # trying to make it so is what broke this first: "the middle of the
        # second display" is off the kernel's clamp on a CGA-second machine
        # (the union is not a rectangle - SPEC.md 39.15.4), and os88mouse
        # refuses a target it cannot reach rather than clicking into nothing.
        #
        # The WINDOWED capture is taken with the pointer already on the game,
        # where the keypress needs it: parking it elsewhere and moving it in
        # afterwards left the arrow's own 36 pixels on the PRIMARY as a
        # difference between the windowed and fullscreen frames, which is
        # precisely the measurement this run exists to make.
        park = (4, MBAR_H + 4)
        mo.to(wx + ww // 2, wy + wh // 2)
        os88marty.settle(m, card=sec)
        before = state(m, "windowed", both, t)

        # --- F ---------------------------------------------------------------
        m.key(fskey)
        time.sleep(5)
        m.pause()
        fs_flag = m.read(S("wm_fs"), 2)
        print("   [wm_fs]=%d  (neither app takes the 11.2 surface; the "
              "bracket is 53's alone)" % u16(fs_flag))
        m.run()
        full = state(m, "fullscreen", both, t)
        for c in both:
            w, h, d = m.fbuf(card=c)
            os88marty.write_png_rgb("/tmp/fsx-2-full-card%d.png" % c,
                                    w, h, d)

        # --- and out --------------------------------------------------------
        m.key("Escape")
        time.sleep(5)
        os88marty.settle(m, card=sec)
        mo.to(*park)
        os88marty.settle(m, card=sec)
        after = state(m, "back to windowed", both, t)
        for c in both:
            w, h, d = m.fbuf(card=c)
            os88marty.write_png_rgb("/tmp/fsx-3-after-card%d.png" % c,
                                    w, h, d)

        # --- the discriminator: a repaint nothing here asked for ------------
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.close_panel(m, mo, S, os88marty.settle)
        mo.to(*park)
        os88marty.settle(m, card=sec)
        forced = state(m, "after a forced repaint", both, t)
        for c in both:
            w, h, d = m.fbuf(card=c)
            os88marty.write_png_rgb("/tmp/fsx-4-forced-card%d.png" % c,
                                    w, h, d)

        print()
        bad = []
        # 0. WHICH MONITOR DID THE FULLSCREEN LAND ON? This is the assertion
        # the round-trip one cannot make, and the defect it caught: SPEC.md
        # 53.6's exit repaint puts both cards right, so a comparison taken
        # AFTER the trip is clean whether or not the bracket drew on the wrong
        # display for its whole session. The bracket's own card must change a
        # lot and the other must not change at all.
        own, oth = (sec, pri) if a.far else (pri, sec)
        pairs = [(own, "the bracket's own", True)]
        if not one:
            pairs.append((oth, "the OTHER", False))
        for c, name, want in pairs:
            skip = MBAR_H if c == pri else 0     # the primary's clock moves
            w0 = m.fbuf(card=c)[0]
            base = skip * w0 * 3
            n = sum(1 for i in range(base, len(full["fb"][c]), 3)
                    if full["fb"][c][i:i + 3] != before["fb"][c][i:i + 3])
            ok = (n > 1000) if want else (n == 0)
            print("   fullscreen changed %-18s card by %7d pixel(s)  %s"
                  % (name, n, "ok" if ok else "*** WRONG DISPLAY ***"))
            if not ok:
                bad.append("the bracket drew %s on %s card"
                           % ("nothing" if want else "something",
                              "its own" if want else "the other"))

        # 1. THE MODE, and 2. the display records - the two things a repaint
        # comparison cannot see (see state()'s header).
        for c in both:
            if after["geom"][c] != before["geom"][c]:
                print("   card %d rasters %dx%d, was %dx%d  *** MODE ***"
                      % ((c,) + after["geom"][c] + before["geom"][c]))
                bad.append("card %d is in a different mode afterwards" % c)
        if after["ctx"] != before["ctx"]:
            print("   the vid_ctx records changed across the round trip:")
            print("     before %s" % before["ctx"].hex())
            print("     after  %s" % after["ctx"].hex())
            bad.append("the display records changed")
        # THE LIVE BLOCK IS NOT AN INVARIANT and asserting on it was a
        # harness bug: [vid_cur] follows the DRAWING (vid_ctx_act swaps the
        # block in whenever a primitive touches the other display), so the
        # windowed capture can legitimately hold the Hercules' geometry -
        # seg B000, stride 90, 720x348 - where the one after the trip holds
        # the VGA's. It is only a defect when the SAME display is current at
        # both ends and the geometry still moved.
        if after["live"] != before["live"]:
            print("   the live block is display %d's, was display %d's"
                  % (after["cur"], before["cur"]))
            if after["cur"] == before["cur"]:
                print("     before %s" % before["live"].hex())
                print("     after  %s" % after["live"].hex())
                bad.append("display %d's live geometry changed"
                           % after["cur"])

        rt = [(pri, "primary", MBAR_H)]
        if not one:
            rt.append((sec, "second", 0))
        for c, name, skip in rt:
            # The menu bar is the PRIMARY's and carries a clock, which moves
            # between captures seconds apart - so the comparison starts below
            # it. The Hercules has no bar and is compared whole.
            w0 = m.fbuf(card=c)[0]
            base = skip * w0 * 3
            n = sum(1 for i in range(base, len(after["fb"][c]), 3)
                    if after["fb"][c][i:i + 3] != forced["fb"][c][i:i + 3])
            b = sum(1 for i in range(base, len(after["fb"][c]), 3)
                    if after["fb"][c][i:i + 3] != before["fb"][c][i:i + 3])
            print("   %-9s after the round trip vs a forced repaint: "
                  "%d differing pixel(s)  %s   (vs BEFORE the trip: %d - "
                  "information, not an assertion)"
                  % (name, n, "ok" if not n else "*** CORRUPT ***", b))
            if n:
                bad.append("%s differs from a fresh repaint" % name)
        print("   PNGs in /tmp/fsx-*.png")
        print()
        print("FAIL: %s" % "; ".join(bad) if bad else
              "PASS: both cards match a fresh repaint after the round trip")
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
