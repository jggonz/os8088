#!/usr/bin/env python3
"""A committing keystroke redraws the Disk window; a refused one does not.
(SPEC.md 22.13.3)

    make && python3 tests/fmcommit.py [machine] [system-image]

`fm_onkey`'s editor path banks `fm_editkey`'s answer and reads it after the
modal-dialog test, because that test is a `cmp` and a `cmp` against zero
CLEARS the carry every time - so the `jc` under it was dead code, and every
command the status-line editor commits took the cheap one-line path instead
of the repaint it is owed. Delete removed the file and left its row on the
glass; Rename stored the new name under the old one; New Folder created a
folder that did not appear.

THE INSTRUMENT IS A BREAKPOINT on `fm_repaint`, `tests/assocopen.py`'s, and
for its reason: it costs the shipped kernel nothing at all.

Three cases, and the third is what stops the fix from being a step backwards:

  1. A DELETE confirmed with the second `Del` MUST reach `fm_repaint`, and
     the file must be gone from the window's own cache - which is what says
     the operation happened at all rather than the keystroke being swallowed.
  2. NEW FOLDER, typed and committed with Enter, must reach it too, and the
     folder must appear in the cache. A second body, because Delete's is
     `.del`'s branch and this one is `dskw_mkdir`'s.
  3. A TYPED CHARACTER must NOT reach it, accepted or refused. An accepted
     one lands on the status line and `fm_status_only` draws that line; a
     REFUSED one - a comma, which is not in the FAT set - moves nothing at
     all. Both used to answer "the caller owes the whole window" and it cost
     nothing only because the branch that reads that answer was dead, so
     this is the leg that says the fix bought the owed repaints back without
     buying one nobody owes.

It reads no framebuffer, so it answers for all three adapters out of one run.
"""
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/apps360.img"
S = os88sym.linear
FOLDER = "APPS"                 # B:\APPS: fourteen files, all of them ours
NEWDIR = "GATE"                 # ...and a name no shipped disk carries
fails = []


def say(s):
    print("  " + s)


def bp_key(m, key):
    """Send one key with a breakpoint armed on fm_repaint; did it stop?

    The breakpoint is CLEARED and the machine restarted before returning,
    whether or not it fired - a guest left standing at a breakpoint makes
    every later step of the caller time out instead of reporting this one.
    """
    m.bp_exec("fm_repaint")
    try:
        m.key(key)
        state = m.wait_stop(8.0)
    finally:
        m.bp_exec()
        m.run()
    os88marty.settle(m)
    return state is not None


def names(m):
    return [n for n, _ in dispcp.listing(m, S)]


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, FOLDER)
    say("B:\\%s = %r" % (FOLDER, names(m)))

    # --- 1. a confirmed DELETE ---------------------------------------------
    doomed = next((n for n in names(m) if n.endswith(".O88")), None)
    if doomed is None:
        sys.exit("fmcommit: no package in B:\\%s - this disk cannot answer "
                 "the question" % FOLDER)
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                           dispcp.row_of(m, S, doomed))
    mo.click(*dispcp.row_xy(wx, wy, row))
    os88marty.settle(m)
    m.key("Delete")                     # arms the confirmation (SPEC.md 22)
    os88marty.settle(m)
    hit = bp_key(m, "Delete")           # ...and answers it
    gone = doomed not in names(m)
    say("delete %-12s -> fm_repaint %s, gone from the cache %s"
        % (doomed, "HIT" if hit else "NOT HIT", "yes" if gone else "no"))
    if not gone:
        fails.append("the confirmed Delete did not remove %s from the "
                     "window's cache - the keystroke was swallowed, so the "
                     "repaint answer below is about nothing" % doomed)
    elif not hit:
        fails.append("a confirmed Delete did not reach fm_repaint: the row it "
                     "removed is still on the glass, and the window's cache "
                     "and its pixels now disagree about what is in this "
                     "folder (SPEC.md 22.13.3/22.8)")

    # --- 2. NEW FOLDER, which is a second body -----------------------------
    m.key("KeyN")                       # arms the name editor (SPEC.md 22)
    os88marty.settle(m)
    for ch in NEWDIR:
        m.key("Key" + ch)
    os88marty.settle(m)
    hit = bp_key(m, "Enter")
    made = NEWDIR in names(m)
    say("new folder %-8s -> fm_repaint %s, in the cache %s"
        % (NEWDIR, "HIT" if hit else "NOT HIT", "yes" if made else "no"))
    if not made:
        fails.append("New Folder did not create %s - so the repaint answer "
                     "beside it is about nothing" % NEWDIR)
    elif not hit:
        fails.append("New Folder did not reach fm_repaint: the folder it made "
                     "is in the window's cache and not on its glass "
                     "(SPEC.md 22.13.3)")

    # --- 3. ...and a TYPED character must draw nothing but the line --------
    # A comma is not in the FAT set, so dskw_char refuses it and fm_editkey
    # takes .nochg; a letter is taken and lands on the status line. Neither
    # is the window's business, and both answered .out before 22.13.3.
    before = names(m)
    m.key("KeyN")                       # any one-line prompt will do
    os88marty.settle(m)
    for key, label in (("KeyX", "accepted 'X'"), ("Comma", "refused ','")):
        hit = bp_key(m, key)
        say("%-14s      -> fm_repaint %s"
            % (label, "HIT" if hit else "not hit"))
        if hit:
            fails.append("%s repainted the whole Disk window: a typed "
                         "character changes the status line and nothing else, "
                         "so 11.3's cheap path is the whole of what it owes "
                         "(SPEC.md 22.13.3)" % label)
    m.key("Escape")
    os88marty.settle(m)
    if names(m) != before:
        fails.append("the typed keystrokes changed the listing - this case "
                     "is not testing what it says it is")

if fails:
    print("\nfmcommit: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nfmcommit: a committing keystroke redraws the window and a refused "
      "one does not - PASS on %s" % MACHINE)
