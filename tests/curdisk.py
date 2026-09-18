#!/usr/bin/env python3
"""SPEC.md 7.4: the arrow TRACKS the hand through a disk transfer.

    make && python3 tests/curdisk.py

A file operation freezes the machine (SPEC.md 12.8, 18) and the pointer used
to freeze with it - worse than freeze, once the operation moved FPG_WARM = 3
sectors the progress widget armed, `fpg_paint` spent `gfx_lock`'s promised
hide, and the arrow LEFT THE SCREEN for the rest of the freeze.

THIS ROW BUILDS `make NOCURDISK=1` ITSELF and puts the default kernel back,
because a one-armed reading here is worth very little.  Both claims below are
about a DIFFERENCE - "the arrow moved while the lock was held" is only
interesting against a kernel where it provably cannot - and dispseam.py's
record is the reason the A/B is not optional: a null result is evidence about
the TEST until the test is shown to contain the case.

WHY IT IS NOT A SCREENSHOT.  What changed is not what a frame looks like but
WHEN it changes, and both kernels draw the identical arrow at the identical
place given the identical mouse position.  So this samples the kernel's own
state through the freeze instead - `[gfx_lock_flag]`, `[fpg_on]`,
`[cur_level]` and `[cur_drawn_x]`/`[cur_drawn_y]` - and asks two questions of
the samples:

  1. IS THE ARROW STILL ON THE GLASS while the widget is up?  `[cur_level]`
     < 0 is hidden.  On the old kernel this is 0% by construction: fpg_paint
     hides unconditionally at arm time, BEFORE any of the operation's disk
     work.  On the new one the hide is owed only when the arrow could reach
     the menu bar (SPEC.md 7.4.3), and this test parks it far below.
  2. DOES IT MOVE while `[gfx_lock_flag]` is set?  A change in
     `[cur_drawn_x]`/`[cur_drawn_y]` between two consecutive samples that both
     saw the lock held is a cursor move inside a lock hold, which mou_apply's
     first compare makes unreachable on every kernel before SPEC.md 7.4.

Question 2 is the headline and question 1 is what a person actually reports.

WHERE A SAMPLE PAIR STOPS BEING EVIDENCE, and it is question 2's whole
soundness.  `busy` is read at the two ENDS of a pair and the claim is about
the INTERVAL between them, so a pair is evidence only when the freeze can be
shown to have covered the whole gap - and there is one place in the machine
where it provably does not.  **`gfx_unlock`'s teardown moves the cursor with
the lock still held** (SPEC.md 7.4.5), and it must: its own header says the
order is binding - `fpg_finish` first (which clears `[fpg_on]`), then the
cursor tail that catches the arrow up to the hand, then `.rel` releasing the
flag.  Measured on the NOCURDISK=1 launch leg, that window is **420 guest
cycles** in which a sample reads `lock 1, fpg 0` at a position the hand moved
to during the freeze:

    at cur_lazyend/cursor_show   lock=1 fpg=0 lvl=-1 drawn=(164,69) mouse=(164,27)
    +60 cycles                   lock=1 fpg=0 lvl= 0 drawn=(164,27)
    +420 cycles                  lock=0 fpg=0 lvl= 0 drawn=(164,27)

Paired with the sample before it - busy via `[fpg_on]`, at the old position -
that is a "move during the freeze" that no ISR made.  MEASURED, because "1
move in 97 samples" is the shape of a race and N=1 is not a rate: over 90
recorded NOCURDISK=1 launch legs - 54 at 696e1e49 and 36 at 2d24a373 - the
old rule went red 8 times (7.4% and 11.1%), every one of them the same pair
`(1,1)->(1,0)` at the same place, and the new rule 0 times.  To produce one
on demand rather than waiting for it: break on `cur_lazyend`/`cursor_show`
once the widget has been up 30 samples, and take the leg's next sample inside
the window.

So a pair counts only when the freeze reads the SAME at both ends, and the
four task-level movers there are
(`fpg_finish`'s show, `cur_shape_set`'s pair, `cursor_show`, `cur_lazyend`)
all sit after `fpg_finish` has flipped `[fpg_on]`, so every one of them makes
a pair whose two ends disagree.  Two samples 33 ms apart cannot both land in
a 420-cycle window, so the straddling pair is the only shape they can take.

THIS IS NOT A TOLERANCE.  The threshold stays exactly 0, the straddling pairs
are COUNTED AND PRINTED rather than dropped in silence, and the assertion
keeps every bit of its power: the defect it is looking for - the ISR drawing
through a freeze - shows up in the freeze's INTERIOR, where the default arm
reads 26-39 of them over the same window against this arm's 0.  Recomputed
over ten recorded runs of both scenarios, the new rule costs the default arm
at most ONE move of 26-39 (a `(1,0)->(1,1)` pair at the leading edge, where
the widget armed inside a hold the arrow was already tracking through).

ON MARTYPC, because QEMU cannot time anything (docs/TESTING.md) and because
the whole claim is about what happens while the CPU sits inside the ROM.
Nothing here is a TIMING assertion, though: every figure is a count of
samples, so an oversubscribed host changes none of it.
"""
import atexit
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
import dispcp                                                # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMAGE = "build/os8088-360.img"
APPS = "build/apps360.img"

# How long to watch, and how finely.  One frame is ~16.7 ms of guest time; a
# mount plus a directory walk plus an icon harvest is seconds of it, so this
# is a generous ceiling rather than a tight one - the loop stops as soon as
# the widget goes away.
SAMPLES = 400
DEADBAND = 12           # consecutive samples with the widget down = finished

# Guest frames per sample.  A 1200-baud report is 3 bytes of 7N1 - 22.5 ms,
# which does NOT fit in one 16.7 ms frame (SPEC.md 7.1.4.3's "~25-40 ms"), so
# a one-frame step samples faster than the mouse can possibly report and every
# other sample sees a packet still in flight.
PACKET = 2

# What the default arm has to beat.  Measured on os8088_5150_cga_gla opening
# B:\SYSTEM - 29 moves under the lock and the arrow lit for 18 of 24 widget
# samples (75%) - against NOCURDISK=1's 0 and 0%.  These are a third of that
# and a quarter of it: what they have to separate is "tracking" from "cannot
# move at all", and the tail of any operation legitimately hides the arrow
# again the moment a painter that is NOT confined to the menu bar runs
# (SPEC.md 7.1.4), which here is the window repainting its list.
MOVES_MIN = 5
LIT_MIN = 25.0          # % of the widget-up samples

# The pointer is walked up and down by this much per sample.  Small enough
# that it stays well clear of the menu bar (SPEC.md 7.4.2 refuses a move whose
# cell could reach it, and that refusal would read exactly like the defect).
STEP = 3
SWING = 20              # samples per direction


def say(*a):
    print(*a)
    sys.stdout.flush()


def sample(m, S):
    """(lock, fpg, level, x, y) - one look at the cursor's world."""
    return (m.read(S("gfx_lock_flag"), 1)[0],
            m.read(S("fpg_on"), 1)[0],
            m.read(S("cur_level"), 1)[0],
            int.from_bytes(m.read(S("cur_drawn_x"), 2), "little"),
            int.from_bytes(m.read(S("cur_drawn_y"), 2), "little"))


def classify(s):
    """Split the position changes seen during the freeze into two populations.

    A pair of consecutive samples is evidence about the ISR only if the freeze
    covered the whole interval between them, and `busy` is read at the two
    ENDS.  The one place in the machine where those differ is `gfx_unlock`'s
    teardown (SPEC.md 7.4.5): it takes the widget down, then catches the arrow
    up to the hand, then releases the lock - so for ~420 cycles the machine
    reads busy while a TASK, not the ISR, has just moved the cursor.  Every
    task-level mover there is (`fpg_finish`'s show, `cur_shape_set`'s pair,
    `cursor_show`, `cur_lazyend`) runs after `fpg_finish` has cleared
    `[fpg_on]`, so a pair containing one always shows the freeze state
    CHANGING - and two samples a whole packet apart cannot both land inside
    420 cycles, so that is the only shape it can take.

    So:  moves = the freeze read the same at both ends - the assertion;
         edge  = it did not - reported, never asserted, never silent.

    Nothing here is a threshold: `moves` is still asserted at exactly 0 on
    NOCURDISK=1 and at >= MOVES_MIN on the default arm, and the defect this
    row exists for lives in the freeze's interior, where the default arm reads
    26-39 moves over the same window against the knob arm's 0.
    """
    moves, edge = [], []
    for a, b in zip(s, s[1:]):
        if not ((a[1] or a[0]) and (b[1] or b[0])):
            continue                    # not both inside the freeze
        if (a[3], a[4]) == (b[3], b[4]):
            continue                    # the arrow did not move
        (moves if (a[0], a[1]) == (b[0], b[1]) else edge).append((a, b))
    return moves, edge


def edge_kinds(edge):
    """The freeze-state transitions the straddling pairs crossed, counted."""
    out = {}
    for a, b in edge:
        k = "(%d,%d)->(%d,%d)" % (a[0], a[1], b[0], b[1])
        out[k] = out.get(k, 0) + 1
    return out


def watch(m, S, mo, rx, ry):
    """Start a file operation and sample the cursor's state through it."""
    mo.to(rx, ry)
    m.advance(frames=4)
    base = sample(m, S)
    m.run()                     # `advance` STOPS the guest, and a stopped
                                # guest shifts no UART bits - so the presses
                                # below would never be decoded
    say("  pointer parked at (%d,%d), cur_level %d"
        % (base[3], base[4], base[2] - 256 if base[2] > 127 else base[2]))

    # THE OPERATION, and deliberately not settled: everything interesting
    # happens while it runs.  settle=0 makes dblclick issue the two presses
    # and return instead of sleeping through the very window under test.
    mo.dblclick(rx, ry, settle=0)

    out, quiet, up, seen = [], 0, True, False
    for i in range(SAMPLES):
        m.advance(frames=PACKET)    # ...which also STOPS the guest, so every
        s = sample(m, S)            # read below is of a machine that is not
        out.append(s)               # moving under it
        if s[1] or s[0]:
            seen, quiet = True, 0
        elif seen:
            # THE DEADBAND ONLY COUNTS AFTER THE WIDGET HAS BEEN UP. Counting
            # it from the start ends the run before the operation has warmed
            # past FPG_WARM = 3 sectors, which is a stop this test read as
            # "the widget never armed" - a setup failure reported against a
            # kernel that was working.
            quiet += 1
            if quiet >= DEADBAND:
                break
        # keep the hand moving - this is the input the claim is about
        if i % SWING == SWING - 1:
            up = not up
        m.mouse(dy=-STEP if up else STEP)
    return out


# The two scenarios, and the SECOND one is the lesson: this row shipped with
# only the first, which is the one case in the machine that reads with the gfx
# lock HELD.  A package launch reads with it FREE, and SPEC.md 7.4.2 refused
# that arm on purpose - so a green suite sat beside a pointer that was dead for
# every launch, every assoc open and every package file dialog on the machine.
# One scenario is one path.
SCENARIOS = (
    ("folder", ["SYSTEM"],        "a folder open - reads with the lock HELD"),
    ("launch", ["APPS", "PAINT.O88"], "a package launch - reads with it FREE"),
)


def leg(tree, label, which):
    """One arm of the A/B, against the kernel in `tree`.

    Applying the tree is what points BOTH the lookups below and the library
    helpers they call (`no_saver` resolves `ss_idle` and takes no defines) at
    this kernel rather than at the shipped one.
    """
    tree = tree or os88build.plain()
    tree.apply()
    S = (lambda n: os88sym.linear(n, tree.defines))
    name, path, why = which
    say("\n=== %s: %s ===\n" % (label, why))
    with os88marty.launch(tree.img(os.path.basename(IMAGE)),
                          apps=tree.img(os.path.basename(APPS)),
                          machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)

        # Setup: a Disk window on B:, and a row inside it.  A row is used
        # rather than the desktop's drive zone because a row is far below the
        # menu bar, and SPEC.md 7.4.2's widget test refuses a move near it -
        # measuring there would report the guard as the defect.  The sweep in
        # watch() is bounded for the same reason.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        dx, dy, _, _ = dispcp.win_rect(m, S, disk)
        for step in path[:-1]:
            dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, step)
            dx, dy, _, _ = dispcp.win_rect(m, S, disk)
        entry = dispcp.row_of(m, S, path[-1])
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, dx, dy, entry)
        rx, ry = dispcp.row_xy(dx, dy, row)

        s = watch(m, S, mo, rx, ry)

    # THE FREEZE IS [fpg_on] OR THE LOCK, and not the lock alone: a launch
    # holds no lock at all, so counting lock-held samples measures nothing
    # there and this row read a working kernel as a broken one.
    busy = [x for x in s if x[1] or x[0]]
    moves, edge = classify(s)
    # THE LIT SHARE IS MEASURED OVER THE WIDGET-UP SAMPLES ONLY, because that
    # is the phase in which the old kernel takes the arrow OFF the glass
    # (fpg_paint's unconditional cur_unlazy, SPEC.md 7.4.3).  Measured over the
    # whole freeze it is not a discriminator at all: NOCURDISK=1 leaves the
    # arrow lit for 41% of a folder open and 75% of a launch - LIT AND FROZEN,
    # which is precisely the state SPEC.md 7.1.4.3 rejected, and reading that
    # as "the arrow is fine" is the mistake this test made first.
    wide = [x for x in s if x[1]]
    wlit = [x for x in wide if x[2] < 128]       # cur_level >= 0
    say("  %d samples: %d in the freeze, %d with the widget up"
        % (len(s), len(busy), len(wide)))
    say("  arrow ON THE GLASS for %d of %d widget samples (%s)"
        % (len(wlit), len(wide),
           "%.0f%%" % (100.0 * len(wlit) / len(wide)) if wide else "n/a"))
    say("  arrow MOVED during the freeze %d times" % len(moves))
    if edge:
        say("  ...and %d pair%s straddled a freeze-state change (%s), which "
            "is gfx_unlock's teardown and evidence about neither kernel "
            "(SPEC.md 7.4.5)"
            % (len(edge), "" if len(edge) == 1 else "s",
               ", ".join("%d x %s" % (v, k)
                         for k, v in sorted(edge_kinds(edge).items()))))
    if "--trace" in sys.argv:
        say("    #   lock fpg lvl    x    y")
        for i, x in enumerate(s):
            say("    %-3d %4d %3d %3d %4d %4d"
                % (i, x[0], x[1], x[2] - 256 if x[2] > 127 else x[2],
                   x[3], x[4]))
    return {"n": len(s), "busy": len(busy), "wide": len(wide),
            "lit": len(wlit), "moves": len(moves), "edge": len(edge),
            "name": name}


def main(argv):
    os.chdir(ROOT)
    solo = "--solo" in argv
    fail = []
    new, old = {}, {}

    for sc in SCENARIOS:
        new[sc[0]] = leg(None, "default (SPEC.md 7.4)", sc)

    for k, r in new.items():
        if not r["busy"]:
            fail.append("SETUP [%s]: no sample was taken during a freeze at "
                        "all - neither the gfx lock nor the progress widget "
                        "was ever seen up. The operation was too short or the "
                        "double-click missed the row; nothing below is a "
                        "verdict on SPEC.md 7.4." % k)
    if not fail:
        for k, r in new.items():
            share = 100.0 * r["lit"] / r["wide"] if r["wide"] else 0.0
            if r["moves"] < MOVES_MIN:
                fail.append("[%s] the arrow moved %d times during the freeze "
                            "(want >= %d). That is the whole of SPEC.md 7.4. "
                            "Check 7.4.2's gates - a clip region left armed, "
                            "or a lock held by another task, both defer for "
                            "good reasons and read like this; and 7.4.2.1 is "
                            "the lock-FREE arm the launch scenario covers."
                            % (k, r["moves"], MOVES_MIN))
            if share < LIT_MIN:
                fail.append("[%s] the arrow was on the glass for only %.0f%% "
                            "of the freeze (want >= %.0f%%). SPEC.md 7.4.3 "
                            "keeps fprog's own painters off it and 7.4.3.1 "
                            "puts it BACK when a handler that painted before "
                            "it read had already spent the hide."
                            % (k, share, LIT_MIN))

    if solo:
        say("\ncurdisk: --solo, the NOCURDISK=1 leg is skipped")
    else:
        say("\n--- building the other arm ---")
        # A TREE OF ITS OWN (tools/os88build.py). The `atexit` that used to
        # follow this build - putting the plain kernel back into build/ - is
        # gone with the write it was defending against.
        knob = os88build.tree("NOCURDISK=1")
        for sc in SCENARIOS:
            old[sc[0]] = leg(knob, "NOCURDISK=1, the freeze it "
                             "replaces", sc)

        for k, r in old.items():
            if r["moves"]:
                fail.append("[%s] NOCURDISK=1 moved the arrow %d times during "
                            "the freeze, and it cannot: mou_apply's first "
                            "compare is `cmp byte [gfx_lock_flag], 0 / jne "
                            ".dirty` and [fpg_on] gates the rest. These are "
                            "pairs whose freeze state read the SAME at both "
                            "ends, so gfx_unlock's teardown - the one place a "
                            "task moves the cursor with the lock still held, "
                            "SPEC.md 7.4.5 - is already out of this count and "
                            "is the `straddled` line above instead. So either "
                            "the knob is not reaching the build (check "
                            "VIDSTAMP and KNOBS in the Makefile) or the ISR "
                            "really did draw." % (k, r["moves"]))
            if not r["busy"]:
                fail.append("SETUP [%s]: the NOCURDISK=1 leg never reached a "
                            "freeze either, so the two arms are not "
                            "comparable." % k)
        # AND NOTHING IS ASSERTED ABOUT THE OLD ARM'S LIT SHARE, deliberately.
        # It is not a property that separates the two: NOCURDISK=1 leaves the
        # arrow lit for most of a package launch, because a launch reads with
        # the gfx lock FREE and so never made a promise for anything to spend.
        # What it does not do is MOVE it, and `moves` is asserted at exactly
        # zero above - mou_apply's first compare makes a move under the lock
        # unreachable, and [fpg_on] gates the lock-free case, so that zero is
        # structural rather than merely likely.
        #
        # NOR IS `edge` ASSERTED, on either arm, and that is the other half of
        # `classify`. A straddling pair is a question the samples cannot
        # answer - the freeze changed shape somewhere inside the interval -
        # and on the DEFAULT arm most of them are honest tracking (the widget
        # arming inside a hold the arrow was already following through). It is
        # printed per leg and in the summary below so that a run where it
        # grows is visible rather than silently swallowed.

    say("")
    if fail:
        say("curdisk: %d FAILED" % len(fail))
        for f in fail:
            say("  FAIL: %s" % f)
        return 1
    for k in new:
        o = old.get(k)
        say("curdisk: %-7s moves %d vs %s   widget-phase lit %d/%d vs %s"
            "   straddled %d vs %s"
            % (k, new[k]["moves"], o["moves"] if o else "-",
               new[k]["lit"], new[k]["wide"],
               ("%d/%d" % (o["lit"], o["wide"])) if o else "-",
               new[k]["edge"], o["edge"] if o else "-"))
    say("curdisk: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
