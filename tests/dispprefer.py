#!/usr/bin/env python3
"""Does a window follow the ADAPTER when the adapter changes under it?
(SPEC.md 11.100.1/11.100.4/11.100.5)

    make && python3 tests/dispprefer.py

A SINGLE-CARD test, deliberately - `tests/dispfit.py` gives the same reasoning.
`os8088_xt_vga_herc` has a VGA and a Hercules in it and a VGA answers for the
CGA as well (SPEC.md 39.11.1), so all THREE adapter rows are on the Display
page and the Activate Mode button is the whole apparatus. That is also close to
the machine most people have, and the Display page is where most of them will
meet this.

`apps/browser` is the subject and it is the interesting one, because the thing
it decides once is not a size but a QUESTION: on a CGA a browser asks to hang
over the DOCK (WF_KEEPH, SPEC.md 11.93), because a 155-row desktop band is not
enough to read a page in and the dock's 24 rows are the only ones left. It
asked that once, at launch, from whichever adapter was primary then - so the
same machine on the same adapter gave two different windows depending on how it
got there:

    launched on the CGA        496x179, ends at row 198, over the dock
    switched to the CGA        496x155, ends at row 175, not over it

THREE LEGS, and the third is the one that is not about browser at all:

  A  VGA -> CGA -> VGA. The window must take the dock's rows on the way in and
     give them back on the way out - the flag has to answer BOTH ways, and a
     KEEPH left set on a VGA raises the height ceiling by 24 rows on a screen
     with no shortage of them.
  B  the other launch order, CGA -> VGA. The bank holds 179 and the VGA can
     give 392, so this is the leg the DECLARATION does rather than the flag.
  C  A USER OUTRANKS AN APPLICATION (SPEC.md 11.100.5). Grow the window by hand
     and the round trip must give back the size the USER chose, not the one the
     application publishes - which is 39.11.2.1's promise, and the one thing a
     preference may not break.
  F  `apps/word` and  G  `apps/frotz`, which ride disks of their own (SPEC.md
     68.5, 61.9) - so those two legs run only when the package is on whatever
     `--apps` mounted, and say so when it is not:

         python3 tests/os88disk.py ... build/word.o88 build/WORD.OVL \
                                       build/frotz.o88 -o build/wz360.img
         python3 tests/dispprefer.py --apps build/wz360.img

  E  `apps/texpad`, which is the third shape again: a per-adapter WIDTH (the
     Hercules has 720 columns and a two-pane editor is the one window with
     somewhere to put them), and the tree's first MINIMUM (SPEC.md 11.100.2) -
     two panes with floors of their own cannot be expressed by WMIN_W, which
     is one number for the whole machine and is 96.
  D  `apps/paint`, which is the OTHER shape of conversion and declares no
     preference at all: what goes stale there is not its box - every rect it
     draws already comes off the live one - but the SEVEN FACTS pt_screen takes
     from the adapter, latched once in pt_geom. This leg reads them out of the
     package's own bss, which is the only place they live: a sixteen-colour
     palette on a 1bpp screen is a picture of something wrong rather than an
     absence of something, so a capture cannot ask this question.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88geom                                             # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

TITLE_H = 18
WF_KEEPH = 128
VID_VGA, VID_HERC, VID_CGA = 0, 1, 2
TP_MIN_W, TP_MIN_H, TP_PREV_MIN = 262, 103, 140    # apps/texpad's own equs
ZF_MIN_W, ZF_MIN_H = 200, 120                      # ...and apps/frotz's
WD_MIN_W, WD_MIN_H = 402, 143                      # ...and apps/word's
ZWIN_SLACK = int(re.search(r"^ZWIN_SLACK\s+equ\s+(\d+)",
                           open(os.path.join(os.path.dirname(
                               os.path.dirname(os.path.abspath(__file__))),
                               "apps", "frotz", "zbss.inc")).read(),
                           re.M).group(1))
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def rec(m, slot):
    """x, y, w, h, flags - the flags being what this file is really about."""
    b = m.read(S("wm_wins") + slot * os88geom.WIN_SIZE, os88geom.WIN_SIZE)
    return (u16(b, os88geom.W_X), u16(b, os88geom.W_Y), u16(b, os88geom.W_W),
            u16(b, os88geom.W_H), u16(b, os88geom.W_FLAGS))


def dock(m):
    return u16(m.read(S("vid_dock_y0"), 2))


def switch(m, mo, settle, avail, kind):
    dispcp.open_panel(m, mo, S, settle)
    dispcp.set_primary(m, mo, S, settle, dispcp.adapter_row(avail, kind))
    dispcp.close_panel(m, mo, S, settle)
    settle(m)


def pkg_named(m, want):
    """The segment of the package window whose header names it (SPEC.md 20.2
    puts the 16-byte name at +16). By NAME, because slots are not handed out
    in the order windows were opened."""
    for n in range(os88geom.MAX_WIN):
        got = dispapps.pkg_seg(m, n)
        if got is None:
            return None
        if m.read((got[1] << 4) + 16, 16).split(b"\0")[0] == want:
            return got[1]
    return None


def row_or_none(m, name):
    """dispcp.row_of RAISES when the name is not in the listing, which is an
    answer here rather than an error: this file runs against two different
    apps disks - the shipped one, which is foldered (SPEC.md 19.2), and a
    hand-built one carrying only WORD.O88 and FROTZ.O88."""
    try:
        return dispcp.row_of(m, S, name)
    except RuntimeError:
        return None


def pkg_named(m, want):
    """The segment of the package window whose header names it - SPEC.md 20.2
    puts the 16-byte name at +16. BY NAME, because window slots are not handed
    out in the order windows were opened and this file leaves several package
    windows on the screen as it goes."""
    for n in range(os88geom.MAX_WIN):
        got = dispapps.pkg_seg(m, n)
        if got is None:
            return None
        if m.read((got[1] << 4) + 16, 16).split(b"\0")[0] == want:
            return got[1]
    return None


def kernel_win(m):
    """The last visible window with W_SEG = 0 - a Disk window, not one of the
    packages this file leaves lying about (SPEC.md 20.1: a package owns a
    segment and a kernel window's W_SEG is 0)."""
    b = m.read(S("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
    out = None
    for i in range(os88geom.MAX_WIN):
        o = i * os88geom.WIN_SIZE
        fl = u16(b, o + os88geom.W_FLAGS)
        seg = u16(b, o + os88geom.W_SEG)
        if fl & 3 == 3 and seg == 0:
            out = i
    return out


def find_app(m, mo, settle, name):
    """Put a Disk window on the folder holding `name` and answer (slot, found).

    ONE Disk window for both questions - "is this package here" and "open it".
    Asking them separately opened a second window, left the first on APPS\\ and
    then double-clicked a row position in whichever window came back first.
    """
    before = set(dispcp.win_list(m, S))
    dispcp.open_drive(m, mo, S, settle, "B")
    new = [w for w in dispcp.win_list(m, S) if w not in before]
    dw = new[-1] if new else kernel_win(m)     # open_drive RAISES an existing
    if dw is None:                             # Disk window rather than making
        sys.exit("dispprefer: no Disk window") # a second one on the same
                                               # folder, and the fallback has
                                               # to be a KERNEL window - by
                                               # slot order it was landing on a
                                               # package left over from an
                                               # earlier leg
    wx, wy, ww, _ = dispcp.win_rect(m, S, dw)
    mo.click(wx + ww // 2, wy + TITLE_H // 2)   # to the front: a background
    settle(m)                                   # window re-lists on FOCUS
    if row_or_none(m, name) is not None:        # (SPEC.md 22.8)
        return dw, True
    if row_or_none(m, "APPS") is None:
        return dw, False
    wx, wy, _, _ = dispcp.win_rect(m, S, dw)
    dispcp.open_named(m, mo, S, settle, wx, wy, "APPS")
    settle(m)
    return dw, row_or_none(m, name) is not None


def open_here(m, mo, settle, dw, name):
    """...and open it out of the Disk window find_app already stood on.

    ONE find_app per leg. Calling it twice - once to ask whether the package
    is there and once to open it - opened a SECOND Disk window on the B: root,
    raised that instead, and then scrolled nothing while double-clicking a row
    index resolved against the other window's listing.
    """
    before = set(dispcp.win_list(m, S))
    wx, wy, ww, _ = dispcp.win_rect(m, S, dw)
    mo.click(wx + ww // 2, wy + TITLE_H // 2)   # RAISED: dispcp.open_named's
    settle(m)                                   # own header says the scroll
                                                # arrows only reach a frontmost
                                                # window, and a leg that opened
                                                # a package before this one has
                                                # left that package on top
    wx, wy, _, _ = dispcp.win_rect(m, S, dw)
    dispcp.open_named(m, mo, S, settle, wx, wy, name)
    settle(m)
    sl = [w for w in dispcp.win_list(m, S) if w != dw and w not in before]
    if not sl:
        sys.exit("dispprefer: %s did not open" % name)
    return sl[-1]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        avail = m.read(S("vid_avail"), 1)[0]
        if not (avail & (1 << VID_CGA)) or m.read(S("vid_kind"), 1)[0] != VID_VGA:
            sys.exit("dispprefer: %s must come up on a VGA that also offers "
                     "the CGA row (avail = 0x%02X)" % (a.machine, avail))

        def show(tag, slot):
            x, y, w, h, fl = rec(m, slot)
            say("%-28s %dx%-3d at (%d,%d), ends %d, dock at %d, KEEPH %d"
                % (tag, w, h, x, y, y + h, dock(m), 1 if fl & WF_KEEPH else 0))
            return x, y, w, h, fl

        # --- A: launched on the VGA, switched to the CGA and back ------------
        dwbr, ok = find_app(m, mo, settle, "BROWSER.O88")
        if not ok:
            say("BROWSER.O88 is not on this disk - skipped")
        else:
            print("\nA. launched on the VGA, then Activate Mode both ways")
            sl = open_here(m, mo, settle, dwbr, "BROWSER.O88")
            _, _, _, vh, fl = show("launched on the VGA", sl)
            if fl & WF_KEEPH:
                fail.append("A: it asked to keep its height on a VGA")
            switch(m, mo, settle, avail, VID_CGA)
            _, _, _, ch, fl = show("...switched to the CGA", sl)
            if not fl & WF_KEEPH:
                fail.append("A: on the CGA it did NOT ask for the dock's rows, so "
                            "it is %d rows tall where a browser launched here is "
                            "179 (SPEC.md 11.98)" % ch)
            elif ch <= dock(m) - 20 - 1:
                fail.append("A: WF_KEEPH is set and the frame is still inside the "
                            "band at %d rows" % ch)
            switch(m, mo, settle, avail, VID_VGA)
            _, _, _, back, fl = show("...and back to the VGA", sl)
            if fl & WF_KEEPH:
                fail.append("A: WF_KEEPH survived onto the VGA, where it raises "
                            "the height ceiling by the dock's 24 rows for nothing")
            if back != vh:
                fail.append("A: the round trip changed the height, %d -> %d"
                            % (vh, back))
            bx, by, _, _, _ = rec(m, sl)        # ...and CLOSED, or leg B opens
            mo.click(bx + 13, by + 9)           # nothing: a second launch of a
            settle(m)                           # package at its instance cap
                                                # FRONTS the window that is
                                                # already there (SPEC.md 29),
                                                # so leg B would silently
                                                # re-measure leg A's window and
                                                # its "other launch order"
                                                # would be the same order

            # --- B: the other launch order --------------------------------------
            print("\nB. the other launch order: born on the CGA, switched to the VGA")
            switch(m, mo, settle, avail, VID_CGA)
            sl2 = open_here(m, mo, settle, dwbr, "BROWSER.O88")
            _, _, _, born, fl = show("launched on the CGA", sl2)
            if not fl & WF_KEEPH:
                fail.append("B: a browser launched on a CGA did not ask for the "
                            "dock's rows at all")
            switch(m, mo, settle, avail, VID_VGA)
            _, _, _, grew, fl = show("...switched to the VGA", sl2)
            if grew <= born:
                fail.append("B: on a 480-row screen it is still %d rows - the bank "
                            "holds the CGA's size and only the DECLARATION can "
                            "answer this leg (SPEC.md 11.100.1)" % grew)

            # --- C: a user outranks an application ------------------------------
            print("\nC. ...and then the user sizes it by hand (SPEC.md 11.100.5)")
            x, y, w, h, _ = rec(m, sl2)
            mo.drag(x + w - 8, y + h - 8, x + w - 8 - 120, y + h - 8 - 90)
            settle(m)
            _, _, uw, uh, _ = show("grown by hand", sl2)
            if uw >= w or uh >= h:
                sys.exit("dispprefer: the grow did not take (%dx%d -> %dx%d), so "
                         "leg C would pass without testing anything"
                         % (w, h, uw, uh))
            switch(m, mo, settle, avail, VID_CGA)
            show("...switched to the CGA", sl2)
            switch(m, mo, settle, avail, VID_VGA)
            _, _, rw, rh, _ = show("...and back to the VGA", sl2)
            if (rw, rh) != (uw, uh):
                fail.append("C: the round trip gave back %dx%d where the USER "
                            "chose %dx%d - a preference overruled a person "
                            "(SPEC.md 11.100.5)" % (rw, rh, uw, uh))

            # --- D: the facts a package takes from the adapter -------------------
        dwpt, ok = find_app(m, mo, settle, "PAINT.O88")
        if not ok:
            say("PAINT.O88 is not on this disk - skipped")
        else:
            print("\nD. apps/paint: seven facts latched from the adapter (SPEC.md 11.98)")
            base = dispapps.img_size("paint")
            names = ["pt_mono", "pt_ncol", "pt_scrw", "pt_scrh", "pt_dockr",
                     "pt_cwmax", "pt_chmax"]
            bytes_ = ("pt_mono", "pt_ncol")

            def facts(m, seg):
                out = {}
                for n in names:
                    w = 1 if n in bytes_ else 2
                    b = m.read((seg << 4) + base + dispapps.bss_off("paint", n), w)
                    out[n] = b[0] if w == 1 else u16(b)
                return out

            open_here(m, mo, settle, dwpt, "PAINT.O88")
            seg = None                  # ...found by NAME, not by position. Legs A
            for n in range(os88geom.MAX_WIN):   # and B each leave a browser behind
                got = dispapps.pkg_seg(m, n)    # and slots are not handed out in
                if got is None:                 # the order windows were opened, so
                    break                       # an index counted from either end
                nm = m.read((got[1] << 4) + 16, 16)     # is a guess. +16 is the
                if nm.split(b"\0")[0] == b"PAINT":      # package header's own name
                    seg = got[1]                        # field (SPEC.md 20.2), and
                    break                               # reading the WRONG segment
            if seg is None:                             # at Paint's bss offsets
                sys.exit("dispprefer: no PAINT package window - is it open?")
            show_f = lambda t, v: say("%-28s %s" % (t, "  ".join(
                "%s=%d" % (k, v[k]) for k in names)))
            onvga = facts(m, seg)
            show_f("launched on the VGA", onvga)
            switch(m, mo, settle, avail, VID_CGA)
            oncga = facts(m, seg)
            show_f("...switched to the CGA", oncga)
            if oncga["pt_mono"] != 1 or oncga["pt_ncol"] != 3:
                fail.append("D: on a 1bpp adapter Paint still believes it has %d "
                            "colours - SPEC.md 39.4's dither class is a "
                            "checkerboard there and a 1px stroke of it has nothing "
                            "left" % oncga["pt_ncol"])
            vh = u16(m.read(S("vid_h"), 2))
            if oncga["pt_scrh"] != vh or oncga["pt_dockr"] != dock(m):
                fail.append("D: its SPEC.md 53 surface is still %dx%d with the "
                            "dock at %d, on a screen that is %d rows with its dock "
                            "at %d" % (oncga["pt_scrw"], oncga["pt_scrh"],
                                       oncga["pt_dockr"], vh, dock(m)))
            if oncga["pt_chmax"] >= onvga["pt_chmax"]:
                fail.append("D: the canvas ceiling is still %d rows on a desktop "
                            "band of %d" % (oncga["pt_chmax"], dock(m) - 20 - 1))
            switch(m, mo, settle, avail, VID_VGA)
            back = facts(m, seg)
            show_f("...and back to the VGA", back)
            for k in names:
                if back[k] != onvga[k]:
                    fail.append("D: %s came home as %d rather than %d - a fact "
                                "re-derived on the way out has to answer BOTH ways"
                                % (k, back[k], onvga[k]))

            # --- E: a per-adapter WIDTH, and the tree's first minimum ------------
        dwtp, ok = find_app(m, mo, settle, "TEXPAD.O88")
        if not ok:
            say("TEXPAD.O88 is not on this disk - skipped")
        else:
            print("\nE. apps/texpad: a width per adapter and a floor under the grow box")
            tp = open_here(m, mo, settle, dwtp, "TEXPAD.O88")
            tpbase = dispapps.img_size("texpad")

            def split(m):
                seg = None
                for n in range(os88geom.MAX_WIN):
                    g = dispapps.pkg_seg(m, n)
                    if g is None:
                        break
                    if m.read((g[1] << 4) + 16, 16).split(b"\0")[0] == b"TEXPAD":
                        seg = g[1]
                rd = lambda nm: u16(m.read((seg << 4) + tpbase +
                                           dispapps.bss_off("texpad", nm), 2))
                return rd("tp_cw"), rd("tp_split")

            def shape(tag):
                x, y, w, h, _ = rec(m, tp)
                cw, sp = split(m)
                say("%-28s %dx%-3d  content %d, split %d, preview pane %d"
                    % (tag, w, h, cw, sp, cw - sp))
                return w, h, cw - sp

            vw, _, _ = shape("opened on the VGA")
            switch(m, mo, settle, avail, VID_HERC)
            hw, _, _ = shape("...switched to the Hercules")
            if hw <= vw:
                fail.append("E: %d columns wide on a 720-column screen where it "
                            "was %d on a 640 one - the declaration did nothing "
                            "(SPEC.md 11.100.1)" % (hw, vw))
            switch(m, mo, settle, avail, VID_CGA)
            cw2, _, _ = shape("...switched to the CGA")
            if cw2 != vw:
                fail.append("E: its CGA entry is a PAIR OF ZEROS, so the template "
                            "should stand at %d and it came out %d" % (vw, cw2))
            switch(m, mo, settle, avail, VID_VGA)
            shape("...and back to the VGA")

            x, y, w, h, _ = rec(m, tp)
            for _ in range(3):
                mo.drag(x + w - 8, y + h - 8, x + w - 8 - 200, y + h - 8 - 100)
                settle(m)
                x, y, w, h, _ = rec(m, tp)
            fw, fh, pane = shape("...dragged as small as it goes")
            if fw < TP_MIN_W or fh < TP_MIN_H:
                fail.append("E: the grow box took it to %dx%d, under the %dx%d it "
                            "declared (SPEC.md 11.100.2)" % (fw, fh, TP_MIN_W,
                                                            TP_MIN_H))
            if pane < TP_PREV_MIN:
                fail.append("E: the preview pane is %d pixels wide - at WMIN_W's "
                            "96 the content is 94, tp_clamp_split pins the split "
                            "at 120 and this comes out NEGATIVE" % pane)
            switch(m, mo, settle, avail, VID_HERC)
            gw, _, _ = shape("...and now to the Hercules")
            if gw != fw:
                fail.append("E: a user had sized it to %d and the Hercules "
                            "preference put %d back (SPEC.md 11.100.5)" % (fw, gw))

            # --- F: apps/word, if this disk carries it --------------------------
        print("\nF. apps/word: a Hercules width and a floor under the chrome")
        dwwd, ok = find_app(m, mo, settle, "WORD.O88")
        if not ok:
            say("WORD.O88 is not on this disk - skipped (see the header)")
        else:
            wd = open_here(m, mo, settle, dwwd, "WORD.O88")
            x, y, w, h, _ = rec(m, wd)
            say("opened on the VGA            %dx%d" % (w, h))
            vgaw = w
            switch(m, mo, settle, avail, VID_HERC)
            _, _, hw, hh, _ = rec(m, wd)
            say("...switched to the Hercules  %dx%d" % (hw, hh))
            if hw <= vgaw:
                fail.append("F: %d columns wide on a 720-column screen where "
                            "it was %d on a 640 one" % (hw, vgaw))
            switch(m, mo, settle, avail, VID_VGA)
            x, y, w, h, _ = rec(m, wd)
            for _ in range(5):     # ...and only NOW, because growing it by
                tx = max(x + 24, x + w - 8 - 130)   # hand stops the preference
                ty = max(y + 24, y + h - 8 - 90)    # applying at all (11.100.5)
                mo.drag(x + w - 8, y + h - 8, tx, ty)
                settle(m)
                x, y, w, h, _ = rec(m, wd)
            say("...dragged as small as it goes %dx%d" % (w, h))
            if w < WD_MIN_W or h < WD_MIN_H:
                fail.append("F: the grow box took it to %dx%d, under the %dx%d "
                            "its ribbon and ruler need" % (w, h, WD_MIN_W,
                                                           WD_MIN_H))

        # --- G: apps/frotz, likewise ----------------------------------------
        print("\nG. apps/frotz: a declared floor, and the adapter latch dropped")
        dwzf, ok = find_app(m, mo, settle, "FROTZ.O88")
        if not ok:
            say("FROTZ.O88 is not on this disk - skipped (see the header)")
        else:
            zf = open_here(m, mo, settle, dwzf, "FROTZ.O88")
            x, y, w, h, _ = rec(m, zf)
            for _ in range(5):
                tx = max(x + 24, x + w - 8 - 130)
                ty = max(y + 24, y + h - 8 - 90)
                mo.drag(x + w - 8, y + h - 8, tx, ty)
                settle(m)
                x, y, w, h, _ = rec(m, zf)
            say("dragged as small as it goes  %dx%d" % (w, h))
            if (w, h) != (ZF_MIN_W, ZF_MIN_H):
                fail.append("G: it floors at %dx%d rather than the %dx%d it "
                            "declares - zf_onsize used to answer this and only "
                            "the grow box ever asked" % (w, h, ZF_MIN_W,
                                                         ZF_MIN_H))
            seg = pkg_named(m, b"FROTZ")
            zbase = dispapps.img_size("frotz") + ZWIN_SLACK
            # zw_geom is reached only while a STORY is being drawn and a test
            # disk carries none, so the latch is armed from outside the guest.
            # That is exactly what the handler contracts to undo.
            m.write((seg << 4) + zbase + 9, bytes([1]))
            say("latch armed by hand          zw_vidok=%d"
                % m.read((seg << 4) + zbase + 9, 1)[0])
            switch(m, mo, settle, avail, VID_CGA)
            got = m.read((seg << 4) + zbase + 9, 1)[0]
            say("...switched to the CGA       zw_vidok=%d" % got)
            if got:
                fail.append("G: [zw_vidok] survived the adapter change, so "
                            "[zw_bpp] still says what the OLD screen was and "
                            "@set_colour is honoured on a 1bpp adapter "
                            "(SPEC.md 39.4)")

    print()
    for f in fail:
        print("dispprefer: FAIL: %s" % f)
    if fail:
        return 1
    print("dispprefer: a window follows the adapter, and stops following it "
          "the moment a user sizes it - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
