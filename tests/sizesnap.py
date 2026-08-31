#!/usr/bin/env python3
"""Does the size snap align a width WITHOUT breaking the zoom? (SPEC.md 11.94.5)

    make && python3 tests/sizesnap.py [--machine os8088_xt_vga]

SPEC.md 11.94 snaps a window's content ORIGIN; 11.94.5 snaps its WIDTH too, so
the LAST cell in a row stops spilling into a second framebuffer byte the way
11.94 already stopped the first one. Three assertions, and the third is the
one this file exists for:

  ALIGNED   every visible window's content width is a multiple of 8, and its
            content origin still is. That is the feature.

  ZOOMED    a maximized window is x = 0, w = [vid_pw] - UNTOUCHED. 11.95.2
            made the standard rect span the screen so the alignment was kept
            rather than bought with eight columns of content; 11.95.3 then
            took the RIGHT border too, so the content of a flush window is
            W_W and is already a multiple of 8. The refusal is therefore no
            longer about an unaligned width - it is that there is nothing to
            round: a snapper that takes this case at all takes EIGHT COLUMNS
            off every maximized window on every adapter, and the window still
            looks maximized - which is why arithmetic rather than a
            screenshot is what catches it.

  FLOORED   nothing came up narrower than the minimum it declared
            (11.100.2). Since 11.94.5.1 the snap rounds UP, so this cannot
            happen by construction and the leg is a guard rather than a
            question - kept because it is the assertion that would fail
            first if the direction were ever put back.

It reads the window records rather than pixels, because all three questions
are about numbers the kernel holds and a 7px difference at the right edge of
a full-screen window is not something an eye finds in a 640x200 dump.

`make NOSIZESNAP=1` is the control: ALIGNED then fails for the windows whose
width was never a multiple of 8, and ZOOMED passes - which is the whole shape
of the defect, and why ZOOMED alone is not a sufficient gate.

WHAT THIS GATE COULD NOT SEE, and it is worth knowing before adding to it:
the snap rounded DOWN when it landed and all three legs passed, because all
three are about the kernel's own numbers. What was wrong was the PACKAGE's -
eleven of thirteen templates size their layout from the width they asked for,
so a shrunk window drew that layout over its own right border (SPEC.md
11.94.5.1). It took a pixel comparison, in tests/cppromise.py, to find it.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp

MACHINE = "os8088_5150_cga_gla"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
WF_NOSNAP, WF_FULL = 16, 8
fails = []


def content_w(win, vid_w):
    """A frame that spans the screen has NEITHER side border.

    11.95.2 for the left and 11.95.3 for the right, which is what `wm_geom`
    answers: it subtracts `wm_bord` and then `wm_bordr`, and both are 0 for a
    flush window. It was `w - 1` while only the left one had gone.
    """
    flush = win.x == 0 and win.x + win.w >= vid_w
    return win.w - (0 if flush else 2)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")   # a Disk window, which
                                                          # is RESIZABLE and so
                                                          # is a zoom subject
    vid_w = os88geom.word(m, "vid_w")
    vid_pw = os88geom.word(m, "vid_pw")
    print("screen %d wide, [vid_pw] %d" % (vid_w, vid_pw))

    # --- ALIGNED: every window that did not opt out ------------------------
    for w in os88geom.windows(m):
        if not w.visible or w.flags & (WF_NOSNAP | WF_FULL):
            continue
        cw, cx = content_w(w, vid_w), w.x + (0 if w.x == 0 else 1)
        ok = cw % 8 == 0 and cx % 8 == 0
        print("  %-22s x=%-4d w=%-4d content x=%-4d w=%-4d  %s"
              % (w.title, w.x, w.w, cx, cw, "ok" if ok else "*** OFF GRID"))
        if not ok:
            fails.append("ALIGNED: %s content is (%d, %d wide)"
                         % (w.title, cx, cw))

    # --- ZOOMED: double-click a title bar and read the record back ---------
    subj = [w for w in os88geom.windows(m)
            if w.visible and not w.flags & WF_FULL]
    if not subj:
        sys.exit("no visible window to zoom - the desktop came up empty")
    z = subj[0]
    before = (z.x, z.y, z.w, z.h)
    mo.dblclick(z.x + z.w // 2, z.y + 6)               # the title bar, clear
    os88marty.settle(m)                                 # of both boxes
    z = [w for w in os88geom.windows(m) if w.i == z.i][0]
    print("zoom: %s %s -> (%d,%d,%d,%d)" % (z.title, before, z.x, z.y, z.w, z.h))
    if (z.x, z.w) != (0, vid_pw):
        fails.append("ZOOMED: maximized to x=%d w=%d, wanted x=0 w=%d"
                     % (z.x, z.w, vid_pw))
    else:
        print("  x=0 w=%d - 11.95.2's rect stands" % vid_pw)

    # --- FLOORED: 11.100.2's declared minimum, per slot --------------------
    for w in os88geom.windows(m):
        if not w.visible:
            continue
        mn = os88geom.word(m, "wm_minw", None) if False else None
        raw = m.read(S("wm_minw") + w.i * 2, 2)
        mn = raw[0] | (raw[1] << 8)
        if mn and w.w < mn:
            fails.append("FLOORED: %s is %d wide, declared %d"
                         % (w.title, w.w, mn))
    print("floors: %s" % ("none broken" if not any(
        f.startswith("FLOORED") for f in fails) else "BROKEN"))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  aligned, zoom untouched, no floor broken")
