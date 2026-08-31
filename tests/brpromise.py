#!/usr/bin/env python3
"""Does the Browser's promise follow the FETCH? (SPEC.md 71.11)

    make && python3 tests/brpromise.py [--machine os8088_xt_vga]

SPEC.md 11.96.1's promise - "my content does not change while I am not
drawing" - reads like a property of a package and is a MODE. The Browser is
the disqualifier written out: its worker advances the page and SKIPS DRAWING
when the window cannot be seen, which is content changing without being
drawn. But only while a fetch is in flight, and [br_nstate] already knows.

So the assertion is a SEQUENCE, and each step is read out of the window
record rather than inferred:

  IDLE   the Browser comes up promising. Nothing has been fetched, so the
         page is standing still by construction.
  LIVE   a Return in the location bar starts a fetch, and the promise is
         WITHDRAWN before the worker can advance anything. This is the half
         that must be prompt: late here and the cache is a picture of the
         past.
  BACK   the fetch settles - and on a machine with no NIC it settles as
         BN_ERR, which is a settled state like any other - and the promise
         returns. Late here costs only a repaint, which is why this half is
         the worker's.

MartyPC has NO NIC of any kind (docs/TESTING.md), so the fetch fails at
net_find and the state goes BN_OPEN -> BN_ERR. That is not a weaker test of
this than a real page would be: the subject is the state machine's second
job, and BN_ERR exercises the same two edges a BN_DONE does.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
fails = []
BN = {0: "BN_IDLE", 1: "BN_OPEN", 2: "BN_WAIT", 3: "BN_SEND", 4: "BN_HEAD",
      5: "BN_BODY", 6: "BN_DONE", 7: "BN_ERR"}


def state(m, br):
    """This window's W_FLAGS, in ONE two-byte read.

    os88geom.windows() decodes every record AND reads each title out of its
    package's segment, which is several round trips - so a loop built on it
    samples every few tens of milliseconds however short its sleep is, and
    the window this test is watching is about fourteen. That is the whole of
    why the first version of this gate reported a promise that was never
    withdrawn while the picture on the glass said the fetch had run.
    """
    f = m.read(S("wm_wins") + br * dispcp.WIN_SIZE + dispcp.W_FLAGS, 2)
    flags = f[0] | (f[1] << 8)
    return bool(flags & 0x20), flags          # WF_SAVEU


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BROWSER.O88")
    os88marty.settle(m)

    br = [x for x in os88geom.windows(m) if "rowser" in x.title]
    if not br:
        sys.exit("the Browser did not open - windows: %r" % os88geom.windows(m))
    br = br[-1]

    p, f = state(m, br.i)
    print("IDLE : flags=%04X promises=%s" % (f, p))
    if not p:
        fails.append("IDLE: the Browser makes no promise before any fetch")

    # A RETURN IN THE LOCATION BAR is br_go's own door (SPEC.md 71.8), and it
    # runs on the UI task with the lock held - which is where the clear is
    # legal (SPEC.md 20.6 rule 7).
    _, _, pre = m.fbuf()
    mo.click(br.x + 60, br.y + 42)          # the location bar: the toolbar's
                                            # buttons occupy content rows 3..15
                                            # (their frames are at y+21..y+33),
                                            # so the bar under them starts
                                            # around content row 24
    os88marty.settle(m)
    m.type_text("http://x.test/")   # br_go answers "Not an http URL"
                                # to anything else, and then no
                                # fetch ever starts - which is a
                                # PASS on every assertion below
                                # and a test of nothing
    m.key("Enter")

    # ...AND THE WITHDRAWAL IS SAMPLED FAST. With no NIC the fetch reaches
    # BN_ERR within a tick or two, so a settle() here would step over the
    # live window entirely and the test would report a pass it never saw.
    # ...AND THE GUEST IS STOPPED TO SAMPLE IT. With no NIC the fetch reaches
    # BN_ERR in a tick or two, and at 4x real time a poll over the debug
    # socket steps clean over the live window - which reads exactly like a
    # promise that was never withdrawn. m.pause() makes the read atomic
    # against the guest instead of racing it.
    _, _, post = m.fbuf()
    moved = sum(1 for a, b in zip(pre, post) if a != b)
    print("       (the click and the Return moved %d subpixels - 0 would mean "
          "the keystroke never reached br_go)" % moved)

    # ...AND SAMPLED FINELY, FREE-RUNNING. The live window is one worker
    # tick wide - br_go sets BN_OPEN on the UI task and the worker's next
    # pass finds no NIC and ends it - which is 55ms of guest time, and this
    # machine runs at about 4x real time. Two things were got wrong here
    # before this comment: a 20ms poll sampled a 14ms window and the gate was
    # a coin toss, and PAUSING between samples made it worse rather than
    # better, because a paused guest does not advance at all and 120 samples
    # went by before the UI task had even seen the keystroke.
    live, n = None, 0
    for _ in range(800):
        p, f = state(m, br.i)
        n += 1
        if not p:
            live = f
            break
        time.sleep(0.002)
    print("LIVE : %s after %d samples"
          % ("flags=%04X promises=False" % live if live is not None
             else "NEVER WITHDRAWN", n))
    if live is None:
        fails.append("LIVE: the promise was never withdrawn - either a fetch "
                     "ran with the raise cache still armed, or br_go refused "
                     "the URL and this run tested nothing")

    os88marty.settle(m)
    time.sleep(1.0)
    os88marty.settle(m)
    p, f = state(m, br.i)
    print("BACK : flags=%04X promises=%s" % (f, p))
    if not p:
        fails.append("BACK: the fetch settled and the promise did not return")

print()
if fails:
    for x in fails:
        print("FAIL  " + x)
    sys.exit(1)
print("PASS  the promise followed the fetch: idle -> withdrawn -> back")
