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
    `limit` is converted to GUEST time by `until`, so a loaded box cannot cut
    the decode's allowance short.
    """
    def now():
        return m.read(S("toast_buf"), 24).split(b"\0")[0]
    try:
        os88marty.until(m, lambda _: now() != was, "toast_buf to change",
                        poll=0.25, limit=limit)
    except os88marty.MartyError:
        return None
    return now()


HANDOVER = "assoc_run_x.handover"     # inlined, size pass 9


def dbl_at_handover(m, mo, wx, wy, name, before_wins):
    """Double-click `name`, and read the machine AT the handover and AT the
    first toast after it - while the guest is STOPPED at each.

    **Both reads happen in the trace's `on_hit`, and they have to.** A
    `bp_trace` PUMPS: it resumes the guest at every stop by itself, so once
    `tr.wait` has seen the handover the machine is already running on into
    the document. This row used to read the screen and arm `toast_now` from
    the body after that wait - so what it called "the screen at the handover"
    was whatever the guest had reached by the time the host got there, and on
    a loaded box Paint had raised 'Decoding GIF' before the second breakpoint
    existed, the first toast caught was 'Opened', and the row failed naming
    SPEC.md 42.14 for a race in its own harness. The callback runs before the
    resume, so a read there IS the stop it belongs to, however slow the host.

    The arm goes BETWEEN the two presses (assocopen.py's `bp_open`, for its
    reasons): the first press legitimately draws, and the final release is
    sent unproven because the guest may by then be standing at a stop.

    Returns {"handover": (new_windows, rect, frame) or None,
             "toast": bytes or None}.
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

    seen = {"handover": None, "toast": None}

    def on_hit(mm, rec):
        if rec["name"] == HANDOVER and seen["handover"] is None:
            new = [w for w in dispcp.win_list(mm, S) if w not in before_wins]
            rect = dispcp.win_rect(mm, S, new[-1]) if new else None
            seen["handover"] = (new, rect, frame(mm))
        elif (rec["name"] == "toast_now" and seen["handover"] is not None
              and seen["toast"] is None):
            # toast_show has staged the message before it calls toast_now,
            # so the buffer IS the toast this stop is drawing
            said = mm.read(S("toast_buf"), len(WANT) + 1)
            seen["toast"] = said.split(b"\0")[0]
            mm.breakpoints([])          # the answer is in: the decode and the
                                        # rest of the load run at full speed

    with os88marty.bp_trace(m, HANDOVER, "toast_now", on_hit=on_hit) as tr:
        mo._edge(True)                  # ...the double-click
        t2 = mo.ticks()
        m.mouse(0, 0, l=False)
        if tr.until(lambda: seen["handover"] is not None, "assoc_handover",
                    limit=30.0, required=False):
            tr.until(lambda: seen["toast"] is not None,
                     "a toast after the handover", limit=60.0, required=False)

    span = (t2 - t1) & 0xFFFFFFFF
    if span >= os88mouse.DBL_TICKS:
        sys.exit("assocwake: the two presses were %d ticks apart and the "
                 "window is %d - the guest saw two FIRST clicks, so nothing "
                 "below is about a double-click at all"
                 % (span, os88mouse.DBL_TICKS))
    return seen


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
    seen = dbl_at_handover(m, mo, wx, wy, DOC, before_wins)
    if seen["handover"] is None:
        sys.exit("assocwake: assoc_handover was never reached - the "
                 "double-click did not land, or the launch failed before "
                 "the handover (SPEC.md 54.10)")
    new, rect, at_handover = seen["handover"]
    if not new:
        sys.exit("assocwake: no new window at assoc_handover - the "
                 "program did not launch, so nothing below is about the "
                 "handover")
    px, py, pw, ph = rect
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

    # --- 2. ...and the DECODE says what it is doing -------------------------
    said = seen["toast"]
    if said is None:
        fails.append("no toast was staged between the handover and the "
                     "end of the load: pt_load owes 'Decoding GIF' at the "
                     "instruction the read ends, where SPEC.md 12.8's "
                     "widget has nothing left to report (SPEC.md 42.14)")
    else:
        say("at toast_now: toast_buf = %r" % said)
        if said != WANT:
            fails.append(
                "the first toast of the load reads %r and not %r - "
                "SPEC.md 42.14 wants the decoder named at the point the "
                "machine goes quiet" % (said, WANT))

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
