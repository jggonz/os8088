#!/usr/bin/env python3
"""A document launch draws the PROGRAM'S WINDOW first (SPEC.md 54.10).

    make && python3 tests/assocwake.py [machine] [system-image]

Double-clicking `MEDIA/OS8088.GIF` used to freeze the machine with the file
manager on screen and nothing else: Paint read and decoded the picture inside
its own ENTRY PROC, which runs under the loader's gfx-lock burst before there
is a window at all. The only thing that moved was SPEC.md 12.8's file-activity
widget - a progress bar with no application behind it - and that is exactly
the complaint.

SPEC.md 54.10 splits the launch in two. `assoc_run` finishes the PROGRAM
(locate, read, entry proc, `wm_show`), and only then calls `assoc_handover`,
which reaches the package's `W_ONWAKE` handler and says "spend the document".
So this gate asks three questions in the order the user sees them:

  1. **At `assoc_handover`, is Paint's window already on the glass?** The
     breakpoint is the instrument: stop the guest at that instruction and the
     document has provably not been read yet, so whatever is on screen is what
     the user was looking at while it was. A new window record is not enough -
     `wm_create` makes one in the entry proc - so this compares PIXELS inside
     the new window's frame against the same rect before the click.
  2. **Does the decode say so?** Continue with a breakpoint on `toast_now`
     (SPEC.md 59.4, the only thing that puts a toast on the glass before the
     machine goes quiet) and read `toast_buf`: it must say `Decoding GIF`
     (SPEC.md 42.14). Nothing else raises a toast between the handover and the
     decode - the loader's own verdict is `LD_OK`, which `toast_say` spells as
     "nothing to report".
  3. **Does the picture arrive?** Run on, and the screen inside the window
     must change again.

Question 1 is the whole point and questions 2 and 3 are what stop it passing
vacuously: a build that never opens the document at all answers 1 perfectly.

1bpp only - it reads `vram`, which is exact on CGA and Hercules and impossible
on VGA (docs/MARTYPC-DEBUG.md). The path it exercises has no adapter branch.
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
DOC = "OS8088.GIF"
WANT = b"Decoding GIF"
fails = []


def say(s):
    print("  " + s)


def frame(m):
    """The 1bpp screen as rows[y][x] of 0/1."""
    _, _, rows = m.vram()
    return rows


def rect_diff(a, b, x, y, w, h):
    """Pixels differing between two captures INSIDE one window's frame.

    Restricted to the rect on purpose: the desktop behind the window changes
    too (the Disk window's selection band, the menu bar's clock), and a
    whole-screen count would pass on a build that drew everything except the
    window this gate is about.
    """
    n = 0
    for yy in range(y, min(y + h, len(a))):
        ra, rb = a[yy], b[yy]
        for xx in range(x, min(x + w, len(ra))):
            if ra[xx] != rb[xx]:
                n += 1
    return n


def wait_toast_changes(m, was, limit):
    """Poll `toast_buf` until it stops saying `was`, and answer what it says.

    None on a timeout. The buffer is not cleared when a toast expires - only
    [toast_on] is - so this cannot answer early on a message going away.
    """
    import time
    t0 = time.time()
    while time.time() - t0 < limit:
        now = m.read(S("toast_buf"), 24).split(b"\0")[0]
        if now != was:
            return now
        time.sleep(0.25)
    return None


def dbl_at_bp(m, mo, wx, wy, name, sym):
    """Double-click `name` with `sym` armed across the second press.

    assocopen.py's `bp_open`, and for its reasons: the arm goes BETWEEN the
    two presses because the first legitimately draws, and the final release is
    sent unproven because by then the guest may be standing at the breakpoint,
    where it decodes nothing and `_edge` could only ever time out.
    """
    entry = dispcp.row_of(m, S, name)
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
    x, y = dispcp.row_xy(wx, wy, row)
    mo.to(x, y)
    os88marty.settle(m)

    if mo.where()[2] & 1:
        mo._edge(False)
    mo._edge(True)
    t1 = mo.ticks()
    mo._edge(False)

    m.bp_exec(sym)
    mo._edge(True)                      # ...the double-click
    t2 = mo.ticks()
    m.mouse(0, 0, l=False)
    state = m.wait_stop(30.0)

    span = (t2 - t1) & 0xFFFFFFFF
    if span >= os88mouse.DBL_TICKS:
        sys.exit("assocwake: the two presses were %d ticks apart and the "
                 "window is %d - the guest saw two FIRST clicks, so nothing "
                 "below is about a double-click at all"
                 % (span, os88mouse.DBL_TICKS))
    return state


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
    names = [n for n, _ in dispcp.listing(m, S)]
    say("A:\\MEDIA = %r" % names)
    if DOC not in names:
        sys.exit("assocwake: no %s in A:\\MEDIA - this disk cannot answer the "
                 "question (SPEC.md 63 puts it there)" % DOC)

    before_wins = dispcp.win_list(m, S)
    before = frame(m)

    # --- 1. the window is on the glass BEFORE the document is read ----------
    try:
        state = dbl_at_bp(m, mo, wx, wy, DOC, "assoc_handover")
        if state is None:
            m.bp_exec()
            m.run()
            sys.exit("assocwake: assoc_handover was never reached - the "
                     "double-click did not land, or the launch failed before "
                     "the handover (SPEC.md 54.10)")
        wins = dispcp.win_list(m, S)
        new = [w for w in wins if w not in before_wins]
        if not new:
            m.bp_exec()
            m.run()
            sys.exit("assocwake: no new window at assoc_handover - the "
                     "program did not launch, so nothing below is about the "
                     "handover")
        px, py, pw, ph = dispcp.win_rect(m, S, new[-1])
        at_handover = frame(m)
        drawn = rect_diff(before, at_handover, px, py, pw, ph)
        area = pw * ph
        say("at assoc_handover: window %d at (%d,%d) %dx%d, %d of %d frame "
            "pixels changed" % (new[-1], px, py, pw, ph, drawn, area))
        if drawn * 100 < area * 20:
            fails.append(
                "the launched window is NOT on the glass at assoc_handover: "
                "only %d of its %d frame pixels differ from the desktop that "
                "was there before the click. wm_show has to have drawn it "
                "before the document is read, or the user waits at a screen "
                "with nothing new on it (SPEC.md 54.10)" % (drawn, area))

        # --- 2. ...and the DECODE says what it is doing ---------------------
        m.bp_exec("toast_now")
        m.run()
        state = m.wait_stop(60.0)
        if state is None:
            fails.append("no toast was staged between the handover and the "
                         "end of the load: pt_load owes 'Decoding GIF' at the "
                         "instruction the read ends, where SPEC.md 12.8's "
                         "widget has nothing left to report (SPEC.md 42.14)")
        else:
            said = m.read(S("toast_buf"), len(WANT) + 1)
            said = said.split(b"\0")[0]
            say("at toast_now: toast_buf = %r" % said)
            if said != WANT:
                fails.append(
                    "the first toast of the load reads %r and not %r - "
                    "SPEC.md 42.14 wants the decoder named at the point the "
                    "machine goes quiet" % (said, WANT))
    finally:
        m.bp_exec()                     # BEFORE the run below, or the next hit
        m.run()                         # stops the machine under the caller

    # **NOT `settle` HERE.** It returns once two frames a second apart are
    # identical, and a decode holds the screen PERFECTLY still for as long as
    # it runs - which is docs/MARTYPC-DEBUG.md's own warning about this
    # routine, and reads as a picture that never arrived. The decode's own
    # toast is the honest signal: it is replaced by `Opened`, or by why not,
    # at the instruction the load ends.
    said = wait_toast_changes(m, WANT, 240.0)
    if said is None:
        fails.append("the decode never finished: toast_buf still reads %r "
                     "after 240 s. pt_onwake replaces it with the outcome at "
                     "the end of the load (SPEC.md 54.10)" % WANT)
    else:
        say("the load ended saying %r" % said)
    os88marty.settle(m)

    # --- 3. ...and the picture arrives --------------------------------------
    wins = dispcp.win_list(m, S)
    new = [w for w in wins if w not in before_wins]
    if not new:
        fails.append("the launched window is gone by the end of the load")
    else:
        qx, qy, qw, qh = dispcp.win_rect(m, S, new[-1])
        after = frame(m)
        landed = rect_diff(at_handover, after, qx, qy, qw, qh)
        say("after the load: window at (%d,%d) %dx%d, %d frame pixels changed "
            "since the handover" % (qx, qy, qw, qh, landed))
        if landed == 0:
            fails.append(
                "nothing changed inside the window between the handover and "
                "the end of the load - the document was never opened, which "
                "is what makes step 1 pass vacuously (SPEC.md 54.10)")

if fails:
    print("\nassocwake: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nassocwake: the program's window is drawn before its document is "
      "read, and the decode says so - PASS on %s" % MACHINE)
