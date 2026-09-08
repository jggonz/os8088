#!/usr/bin/env python3
"""The close negotiation and the standard alert (SPEC.md 75, 27.15).

    make && python3 tests/dispclose.py
    python3 tests/dispclose.py --machine os8088_5150_herc_gla
    python3 tests/dispclose.py --machine os8088_xt_vga
    make small && python3 tests/dispclose.py --small

Eight things, in one boot, on a cycle-accurate 5150:

  1. A CLEAN Note Pad closes exactly as it always did - no alert, no window.
  2. A DIRTY one refuses: the close box leaves the window on screen and puts
     an alert up, titled with Note Pad's own caption.
  3. A SECOND close click raises no new alert and RAISES the one that is up,
     which is what this has instead of modality (SPEC.md 75.3.1).
  4. Cancel leaves it open AND leaves it closable again - which is the one
     thing [np_asking] cannot forget (SPEC.md 27.15).
  5. Dismissing the alert with its own close box is the same answer, and it
     arrives through the alert's own W_ONCLOSE (SPEC.md 75.3) rather than
     through a button.
  6. Save on an UNNAMED note raises the Save As dialog, and its commit is the
     quit (SPEC.md 27.15.2).
  7. Save on a NAMED one writes and closes with no dialog at all.
  8. The bytes on the FLOPPY are the typed ones - read from the host by
     os88flush, not by asking os8088 (docs/FIELD-NOTES.md 4's rule: the writer
     and the reader are the same FAT12 code).

THE ALERT IS FOUND IN THE WINDOW TABLE, not by looking at pixels. It is a
package's own window now (SPEC.md 75.3), so there is no kernel word naming
it - what it IS, in kernel terms, is an UNOWNED window (`wm_owner` = 0xFF)
that is not one of the windows this test already knows about. That is a fact
two builds cannot disagree about the way a screenshot can.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88geom                                            # noqa: E402
import os88build
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88flush                                            # noqa: E402
import dispcp                                               # noqa: E402

TITLE_H = 18
UI_ABW, UI_ABG, UI_ABTNY, UI_ABH = 72, 12, 46, 13   # apps/os88ui.inc
S = os88sym.linear
FAIL = []


# `small_syms` USED TO BE HERE, and it is gone. It built a kern_small map in
# process with `check=False`, because os88sym compares against
# build/kernel.bin and that is the BIG build - so the row went without the
# proof its own docstring described giving up, AND every other tool in the
# session went on answering for the big kernel. The window record is 28 bytes
# on kern_small and 34 on kern_big, so os88geom's stride, tools/os88ui.py's
# window table and dispcp's decode were all reading the wrong one; the row
# patched `dispcp.WIN_SIZE = 28` and nothing else, which was enough while it
# only used dispcp.
#
# $OS88_DEFINES and $OS88_BUILD say it ONCE, in the registry, and every
# consumer follows: os88sym picks the arm AND checks against
# build/smallk/kernel.bin, os88geom's per-arm constants resolve to 28, and
# the map is proven rather than assumed. tests/fcpcopy.py's row has said it
# that way all along.


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        FAIL.append(msg)


def alert_slot(m, live=()):
    """The live alert's window slot, or None (see the module docstring).

    The Standard File dialog is unowned too (SPEC.md 38.1 is the species this
    borrows), and Note Pad raises one from the alert's own Save button - so
    without excluding it, "Save takes the alert down" reads as a failure at
    the exact moment it worked.
    """
    fd = u16(m.read(S("fdlg_win"), 2))
    base = S("wm_wins") - os88sym.KERNEL_SEG * 16
    for i in dispcp.win_list(m, S):
        if i in live or (fd and base + i * dispcp.WIN_SIZE == fd):
            continue
        if m.read(S("wm_owner") + i, 1)[0] == 0xFF:
            return i
    return None


def btn_xy(m, slot, which, n):
    """Centre of button `which` of `n`, laid out by os88ui_arect.

    Re-derived here from the same constants the package uses rather than read
    out of its image, so a layout that drifts is a FAILED CLICK and not two
    copies of one bug agreeing.
    """
    x, y, w, h = dispcp.win_rect(m, S, slot)
    total = n * UI_ABW + (n - 1) * UI_ABG
    bx = x + (w - total) // 2 + which * (UI_ABW + UI_ABG)
    return bx + UI_ABW // 2, y + TITLE_H + UI_ABTNY + UI_ABH // 2


def wait_launch(m, before, tries=30):
    """Wait for a NEW window to appear, and answer its slot.

    open_named settles, and a settle is two identical frames a second apart -
    which a package LOAD satisfies, because the machine is frozen under the
    gfx lock for the whole of it and the screen is perfectly still. So
    "settled" can mean "still loading", and reading the window table there
    finds the Disk window and nothing else. Measured on kern_small, where the
    disk layout makes the load slower than on the big build's image; the big
    build got away with it, which is the worse of the two outcomes.
    """
    for _ in range(tries):
        new = set(dispcp.win_list(m, S)) - before
        if new:
            return sorted(new)[-1]
        os88marty.settle(m)
    return None


def type_text(m, s):
    for ch in s:
        m.key("Key" + ch.upper() if ch.isalpha() else
              {" ": "Space", ".": "Period"}[ch])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=os88build.at("build/npclose.img"))
    ap.add_argument("--small", action="store_true",
                    help="drive kern_small, which has the SAME behaviour now "
                         "(SPEC.md 75.3.2) - the alert being a package's")
    a = ap.parse_args(argv)
    if a.small:
        if a.image == "build/os8088-360.img":
            a.image = "build/small360.img"
        if "KERN_SMALL" not in os.environ.get("OS88_DEFINES", ""):
            sys.exit("dispclose --small needs the kern_small arm declared in "
                     "the environment, which is what makes every tool in the "
                     "session agree about a 28-byte window record:\n"
                     "  OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk \\\n"
                     "      python3 tests/dispclose.py --small")
        # ...and the WINDOW RECORD IS SHORTER THERE: W_ONDRAG, W_ONTIMER and
        # W_TIMER are inside %ifdef KERN_BIG (SPEC.md 13.8.2/13.9), so
        # WIN_SIZE is 28 and not 34. Read with 34 the table looks PLAUSIBLE
        # for slot 0 and is nonsense from slot 1 on, so a visible window reads
        # as "not used" - which cost an hour once, and cost this row a soak
        # when tools/os88ui.py started reading the same table. os88geom
        # resolves it per arm off the environment above; nothing here needs to
        # know the number.

    # ALWAYS rebuilt, never cached on existence: a stale scratch disk runs a
    # Note Pad from an earlier build, and every assertion here is about what
    # Note Pad does - so the gate reports the last build's behaviour and says
    # nothing about this one.
    # THE OUTPUT IS RESOLVED, not just the input (docs/plans/SOAK-PARALLEL.md 14.2).
    # `launch` resolves what it is handed, so writing the literal
    # `build/npclose.img` built the disk in the shared tree and looked for it
    # in the run's own - FileNotFoundError in `_clone`, with the image sitting
    # right there.
    subprocess.check_call([sys.executable, "tools/os88disk.py", "-o",
                           os88build.at(a.apps), "--size", "360",
                           os88build.at("build/notepad.o88")])

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        d = dispcp.win_list(m, S)[-1]
        dx, dy = dispcp.win_rect(m, S, d)[:2]

        # --- 1. a CLEAN Note Pad closes as it always did ---------------------
        before = set(dispcp.win_list(m, S))
        dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "NOTEPAD.O88")
        np = wait_launch(m, before)
        if np is None:
            check(False, "Note Pad did not launch")
            return report()
        known = (d, np)
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        print("  Note Pad at (%d,%d) %dx%d" % (nx, ny, nw, nh))
        mo.click(nx + 9, ny + 9)                       # the close box
        os88marty.settle(m)
        check(np not in dispcp.win_list(m, S),
              "a clean note closes with no alert")
        check(alert_slot(m, (d,)) is None, "...and no alert was raised")

        # --- 2. a DIRTY one refuses and asks ---------------------------------
        before = set(dispcp.win_list(m, S))
        dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "NOTEPAD.O88")
        np = wait_launch(m, before)
        known = (d, np)
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        mo.click(nx + 60, ny + TITLE_H + 12)
        type_text(m, "HELLO")
        os88marty.settle(m)
        mo.click(nx + 9, ny + 9)
        os88marty.settle(m)
        ask = alert_slot(m, known)
        check(np in dispcp.win_list(m, S), "a dirty note REFUSES to close")
        check(ask is not None, "...and an alert is up")
        if ask is None:
            return report()
        ax, ay, aw, ah = dispcp.win_rect(m, S, ask)
        # ...THROUGH SPEC.md 11.94.5's size snap, which rounds 288 to 290.
        # A literal here reads as "that is not the alert", which is what this
        # check exists to say and what it said the day the snap landed.
        check((aw, ah) == (os88geom.snapw(288), 92),
              "the alert is os88ui_ask's window: %dx%d" % (aw, ah))

        # --- 3. a SECOND close click RAISES it (SPEC.md 75.3.1) --------------
        # Drag the note over the alert first, so "on top" is a claim with
        # something to be on top of: wm_zord's last entry is the front window.
        mo.drag(nx + nw // 2, ny + TITLE_H // 2, nx + nw // 2 - 30,
                ny + TITLE_H // 2)
        os88marty.settle(m)
        zn = m.read(S("wm_zn"), 1)[0]
        check(m.read(S("wm_zord"), zn)[zn - 1] == np,
              "the note is in front of its own alert")
        mo.click(nx + 9 - 30, ny + 9)
        os88marty.settle(m)
        zn = m.read(S("wm_zn"), 1)[0]
        check(alert_slot(m, known) == ask, "a second close click raises no "
                                           "NEW alert")
        check(m.read(S("wm_zord"), zn)[zn - 1] == ask,
              "...it RAISES the one that is up")
        nx -= 30

        # --- 4. Cancel: still open, and still closable -----------------------
        bx, by = btn_xy(m, ask, 2, 3)
        mo.click(bx, by)
        os88marty.settle(m)
        check(alert_slot(m, known) is None, "Cancel takes the alert down")
        check(np in dispcp.win_list(m, S), "...and leaves the note open")

        # --- 5. dismissing it is the same answer, through its own W_ONCLOSE --
        mo.click(nx + 9, ny + 9)
        os88marty.settle(m)
        ask = alert_slot(m, known)
        check(ask is not None, "it asks again - [np_asking] was cleared")
        if ask is not None:
            ax, ay = dispcp.win_rect(m, S, ask)[:2]
            mo.click(ax + 9, ay + 9)                   # the alert's close box
            os88marty.settle(m)
            check(alert_slot(m, known) is None,
                  "its own close box dismisses it")
            check(np in dispcp.win_list(m, S),
                  "...and OS88UI_ACANCEL leaves the note open")

        # --- 6. Save on an UNNAMED note asks WHERE (SPEC.md 27.15) -----------
        # [np_named] is what decides, and it is not "has a name": np_defname
        # seeded NOTES.TXT so Ctrl-S always has somewhere to go, and writing
        # that on the way out over whatever the user already had is the thing
        # this leg proves does not happen.
        mo.click(nx + 9, ny + 9)
        os88marty.settle(m)
        ask = alert_slot(m, known)
        if ask is None:
            check(False, "no alert for the Save leg")
            return report()
        bx, by = btn_xy(m, ask, 0, 3)
        mo.click(bx, by)
        os88marty.settle(m)
        check(alert_slot(m, known) is None, "Save takes the alert down")
        check(u16(m.read(S("fdlg_win"), 2)) != 0,
              "...and an UNNAMED note gets the Save As dialog")
        check(np in dispcp.win_list(m, S),
              "...with the note still open behind it")
        m.key("Enter")                      # the dialog's default button, on
        os88marty.settle(m)                 # the name Note Pad handed it
        check(u16(m.read(S("fdlg_win"), 2)) == 0, "the dialog commits")
        check(np not in dispcp.win_list(m, S),
              "...and the SAVE AS was a quit (SPEC.md 27.15.2)")

        # --- 7. ...and a NAMED one just saves --------------------------------
        # Ctrl-S makes the note a file; the alert's Save then writes it with no
        # dialog at all, which is the other half of [np_named].
        before = set(dispcp.win_list(m, S))
        dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "NOTEPAD.O88")
        np = wait_launch(m, before)
        known = (d, np)
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        mo.click(nx + 60, ny + TITLE_H + 12)
        type_text(m, "WORLD")
        m.ctrl("KeyS")
        os88marty.settle(m)
        type_text(m, " AGAIN")
        os88marty.settle(m)
        mo.click(nx + 9, ny + 9)
        os88marty.settle(m)
        ask = alert_slot(m, known)
        check(ask is not None, "a named note that changed again still asks")
        if ask is not None:
            bx, by = btn_xy(m, ask, 0, 3)
            mo.click(bx, by)
            os88marty.settle(m)
            check(u16(m.read(S("fdlg_win"), 2)) == 0,
                  "a NAMED note needs no dialog")
            check(np not in dispcp.win_list(m, S), "...and it closes")

        # --- 8. THE BYTES, off the floppy, with no os8088 code between us ----
        fl = os88flush.Flush(marty=m)
        vol = fl.volume(1)
        names = [e.path for e in vol.walk() if not e.is_dir]
        check("NOTES.TXT" in names,
              "Save wrote NOTES.TXT to the floppy: %s" % names)
        if "NOTES.TXT" in names:
            body = vol.read("NOTES.TXT")
            check(body == b"world again", "...and it holds %r" % body)
    return report()


def report():
    print("dispclose: %d failure(s)" % len(FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
