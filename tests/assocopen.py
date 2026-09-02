#!/usr/bin/env python3
"""Opening a DOCUMENT draws no pixel of the Disk window (SPEC.md 22.13.2).

    make && python3 tests/assocopen.py [machine] [system-image]

Double-clicking `OS8088.GIF` posts an ASSOCIATION (SPEC.md 54.4) and no
package index, so the `[ld_pending] != 0` test all three open paths used to
make - "did this post a package load?" - answered the FOLDER's answer, and
every document open repainted the Disk window whole: a white fill of the
content and some forty `font_str`s, immediately before the program the
document asked for drew its window over the top of them. `fm_open_sel`
answers in CF now, because the branch is its.

THE INSTRUMENT IS A BREAKPOINT, not a counter, so this gate costs the
shipped kernel nothing at all: arm `fm_repaint`, send the double-click, and
ask whether the guest stopped.

Two cases, and the FIRST is what makes the second mean anything:

  1. A FOLDER open MUST hit the breakpoint. `fm_go` really did replace the
     listing, the selection and the caption, so the whole window is owed -
     and a run where the click missed the row, or where the symbol resolved
     somewhere that is never executed, looks exactly like a pass on case 2.
  2. A DOCUMENT open must NOT hit it - and a NEW WINDOW must appear, which
     is what says the double-click landed and was dispatched rather than
     being swallowed.

It reads no framebuffer, so it answers for all three adapters out of one
run; the paths it exercises have no adapter branch in them.
"""
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/apps360.img"
S = os88sym.linear
fails = []


def say(s):
    print("  " + s)


def bp_open(m, mo, wx, wy, name):
    """Double-click `name` with a breakpoint armed across the SECOND press.

    Returns (repainted, span_in_ticks). The pointer is placed and both edges
    of the first press are PROVEN against the guest's own published
    `mouse_btn` (SPEC.md 9.4.3) - a packet clocked into a 1200-baud UART
    while the previous one is in flight is simply dropped, and the only
    difference a caller can see is that the guest did nothing.

    The arm goes between the two presses rather than around both, because
    the FIRST press legitimately draws (it moves the selection band, SPEC.md
    22.2) and the second is the whole question. The final RELEASE is sent
    unproven: by then the guest may be standing at the breakpoint, where it
    decodes nothing and `_edge` could only ever time out.
    """
    entry = dispcp.row_of(m, S, name)
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
    x, y = dispcp.row_xy(wx, wy, row)
    mo.to(x, y)
    os88marty.settle(m)

    if mo.where()[2] & 1:       # a button left down by something else would
        mo._edge(False)         # make the first press no edge at all
    mo._edge(True)
    t1 = mo.ticks()
    mo._edge(False)

    m.bp_exec("fm_repaint")
    try:
        mo._edge(True)          # ...the double-click
        t2 = mo.ticks()
        m.mouse(0, 0, l=False)
        state = m.wait_stop(8.0)
    finally:
        m.bp_exec()             # clear BEFORE the run below, or the next
        m.run()                 # fm_repaint stops the machine under the
                                # caller and every later step times out
    os88marty.settle(m)
    span = (t2 - t1) & 0xFFFFFFFF
    if span >= os88mouse.DBL_TICKS:
        sys.exit("assocopen: the two presses were %d ticks apart and the "
                 "window is %d - the guest saw two FIRST clicks, so nothing "
                 "below is about a double-click at all"
                 % (span, os88mouse.DBL_TICKS))
    return state is not None, span


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    say("A:\\ = %r" % [n for n, _ in dispcp.listing(m, S)])

    # --- 1. the control: a folder MUST repaint ------------------------------
    hit, span = bp_open(m, mo, wx, wy, "MEDIA")
    say("folder  MEDIA      -> fm_repaint %s  (%d ticks apart)"
        % ("HIT" if hit else "not hit", span))
    if not hit:
        fails.append("a FOLDER open did not reach fm_repaint - fm_go replaced "
                     "the listing, the selection and the caption, so the "
                     "window is owed one. Nothing below can be believed: this "
                     "is what a missed click or an unresolved symbol looks "
                     "like (SPEC.md 22.13.2)")

    names = [n for n, _ in dispcp.listing(m, S)]
    say("A:\\MEDIA = %r" % names)
    doc = next((n for n in names if n.endswith(".GIF")), None)
    if doc is None:
        sys.exit("assocopen: no .GIF in A:\\MEDIA - this disk cannot answer "
                 "the question (SPEC.md 63 puts OS8088.GIF there)")

    # --- 2. ...and a document must NOT ---------------------------------------
    before = dispcp.win_list(m, S)
    hit, span = bp_open(m, mo, wx, wy, doc)
    after = dispcp.win_list(m, S)
    say("document %-10s -> fm_repaint %s  (%d ticks apart)"
        % (doc, "HIT" if hit else "not hit", span))
    say("windows %r -> %r" % (before, after))

    # THE CLAIM FIRST, THE SEATBELT SECOND, and that order is not cosmetic.
    # A hit STOPS THE MACHINE, so the load never runs and no window appears -
    # so asking "did a window appear" first reports the seatbelt on the very
    # build the gate exists to catch, and the reader goes looking for a
    # missed click. The two cannot both be wrong: a click that never landed
    # dispatches nothing and so cannot hit the breakpoint either.
    if hit:
        fails.append("a DOCUMENT open repainted the Disk window in full. "
                     "Nothing in it changed: assoc_post_x does no I/O, the "
                     "selection was already on the glass, and SPEC.md 59.5 "
                     "took the verdicts out to the toast (SPEC.md 22.13.2)")
    elif len(after) <= len(before):
        fails.append("no new window after double-clicking %s: the click did "
                     "not land, or the association did not resolve - so the "
                     "'not hit' above is a click that never happened, not a "
                     "repaint that never ran" % doc)

if fails:
    print("\nassocopen: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nassocopen: a document open costs this window nothing, and a folder "
      "open still costs it everything - PASS on %s" % MACHINE)
