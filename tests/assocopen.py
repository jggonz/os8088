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
    """Double-click `name` inside a breakpoint on `fm_repaint`.

    Returns (repainted, span_in_ticks). The WHOLE gesture is
    os88mouse.dblclick's, on both emulators, inside the trace: its presses
    are stepped (MartyPC) or queued (Bochs) on a STOPPED guest with the
    trace's pump held, and a stop it meets is recorded for the trace - so the
    span between the presses is the UART's and the ISR's, not the host's.
    The first press is inside the trace, which is harmless for its target: a
    press on a row moves the selection band (fm_lsel_bar / fm_sel_bar) and
    never reaches fm_repaint.

    It was HAND-MADE on MartyPC - a proven press, a proven release, the arm,
    a proven second press, each edge a poll loop of host round trips - and
    that span was a quantity of the BOX: MartyPC runs several times faster
    than a 5150, so the ~80 host ms those three proofs take on this box were
    7-8 guest ticks against a window of 9, the release's proof alone 1-2 or
    4-5 ticks depending on whether it was seen on the first poll or after a
    20 ms nap. And the ticks it read back were each taken AFTER a proof, so
    the presses' true birth ticks could be further apart than the 7 it
    reported: the folder did not open, half the runs, "7 ticks apart".
    On Bochs a host-spaced gesture is worse still: `sync=slowdown` pays a
    stopped guest's time back as a catch-up sprint, so 0.08 host seconds
    read 9-10 ticks (docs/BOCHS.md, *The clock*).
    """
    entry = dispcp.row_of(m, S, name)
    row = dispcp.scroll_to(m, mo, S, UD, wx, wy, entry)
    x, y = dispcp.row_xy(wx, wy, row)
    mo.to(x, y)
    os88marty.ui_done(m)

    bochs = getattr(m, "bochs", False)
    with os88marty.bp_trace(m, "fm_repaint") as tr:
        # The gesture holds the pump and records its own stops (dblclick),
        # and a trace clears its set on the way out on both paths - so no
        # later fm_repaint stops the machine under the caller
        span = mo.dblclick(x, y, settle=0)
        # A hit, or the UI task asleep with nothing pending across a whole
        # tick: the open's pass is over, so a repaint it owed would have
        # stopped the machine already. (It was `tr.wait(1, limit=30)`, whose
        # PASS for a document was its whole 90-guest-second budget -
        # docs/plans/completed/SOAK-SPEED.md 5.)
        quiet = [None]

        def over():
            if tr.n:
                return True
            if not os88marty.ui_idle(m):
                quiet[0] = None
                return False
            t = os88marty._ktick(m)
            if quiet[0] is None:
                quiet[0] = t
            return t != quiet[0]
        tr.until(over, "fm_repaint, or the open's pass to end", limit=30.0,
                 required=False)
    state = tr.n > 0
    os88marty.ui_done(m)
    # (dblclick raises on a span of DBL_TICKS or more itself)
    return state, (None if bochs else span)


def gap(span):
    """The presses' spacing as reported: Bochs's are queued (bp_open)."""
    return "queued, one GUI pass" if span is None else "%d ticks apart" % span


# every read below is guest memory (or the framebuffer after the UI is
# done), so the boot gate is the kernel's STATE and the dispcp verbs settle
# on ui_done (docs/plans/completed/SOAK-SPEED.md 4)
def UD(m, **_):
    return os88marty.ui_done(m)


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE,
                      boot=False) as m:
    m.run()
    os88marty.kernel_up(m)
    os88marty.no_saver(m)
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, UD, "A")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    say("A:\\ = %r" % [n for n, _ in dispcp.listing(m, S)])

    # --- 1. the control: a folder MUST repaint ------------------------------
    hit, span = bp_open(m, mo, wx, wy, "MEDIA")
    say("folder  MEDIA      -> fm_repaint %s  (%s)"
        % ("HIT" if hit else "not hit", gap(span)))
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
    say("document %-10s -> fm_repaint %s  (%s)"
        % (doc, "HIT" if hit else "not hit", gap(span)))
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
