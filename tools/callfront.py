#!/usr/bin/env python3
"""The SPEC.md 11.96.9 gate - a window called to the front, after a partial draw.

    python3 tools/callfront.py [machine [dir [-D...]]]
    python3 tools/subcheck.py diff DIR-A DIR-B

Reported from the field on PCem/Hercules/8088: the APPS window called to the front
came back with ANOTHER window's content inside it and no title bar over that
content. It is SPEC.md 11.96.9 - wm_su_bank reading a window's content off the
screen after a PARTIAL draw (11.96.6), so the parts of the screen that still held
the covering windows were banked as this window's own. The missing title bar is
the tell: a raise cache holds content and never chrome.

The reporter's steps, and DRAGGING THE NOTE PAD MORE THAN ONCE was called out as
critical, which it is - each drag is another damage pass banking more pollution:

  1. open Drive A          2. open Drive B        3. navigate to APPS
  4. drag some windows     5. raise Drive A       6. double-click README.TXT
  7. drag Note Pad, twice or more                 8. raise APPS  <- broken

THREE WINDOWS WITH PARTIAL OVERLAPS IS THE WHOLE POINT of this session, and why
subcheck.py could not catch the defect: its two Disk windows cascade 16px apart,
so a drag of one across the other leaves damage covering nearly all of the other's
content - the partial restore is very nearly a whole one and the bank comes out
nearly clean. It takes a third window, overlapping both partially, to leave a large
region of a covered window's content untouched.

Every step is derived from the guest (wm_wins / wm_zord / desk_ord_xy), so it runs
on any adapter; the report is Hercules, which is the default here. It captures
after EVERY step, so `rawdiff show` names the step that broke rather than only the
end state, and prints the z-order and the rects beside each - because the first
question about a half-drawn window is whether the RECORD or the PIXELS are wrong.

Diff it against `make REDRAWFULL=1` (pass REDRAWFULL as the third argument for
that run, or every symbol lookup answers for the wrong binary). 0 differing pixels
is the standard. And a pixel diff is the SECOND check here: the first is to render
the cache claim itself and look at it, which is what identified both this and
11.96.7 - a wrong cache is invisible until it is used.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
import os88ui
from os88mouse import Mouse
import sucheck as su
import subcheck as sc

# APPS on B: and README.TXT on A: are found BY NAME in the window's own
# listing: a row ordinal is what SPEC.md 19.4 says nothing may be built on.


def named(m, pre):
    for w in su.windows(m):
        if w.visible and w.title.upper().startswith(pre):
            return w
    return None


def row(ui, win, name):
    """`win`'s visible row for the entry called `name` (os88ui raises, naming
    what the folder holds, when it is not there)."""
    return su.row(win, ui.entry(name, win)[0] - ui.scroll(win))


def opened(m, before, what):
    """A window beyond the `before` visible ones, then its first paint - the
    pixels are what `sc.shot` reads next."""
    os88marty.until(m, lambda _: len(sc.wins(m)) > before, what, poll=0.1,
                    limit=60)
    os88marty.settle(m)


def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla"
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/callfront"
    defines = sys.argv[3:]              # any -D the kernel was built with
    os.makedirs(out, exist_ok=True)
    log = []
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        # THE REFERENCE KERNEL IS A DIFFERENT BINARY, so every symbol has to be
        # looked up with the knob that built it - os88sym asserts its map against
        # build/kernel.bin and refuses rather than answering a plausible wrong
        # address (subcheck.py's own note).
        if defines:
            plain = m.sym
            m.sym = lambda n, d=tuple(defines): plain(n, d)
        mo = Mouse(marty=m)
        ui = os88ui.UI(m, verbose=False, mouse=mo)
        print("machine %s -> %s" % (machine, out))

        n = len(sc.wins(m))
        mo.dblclick(*su.zone(m, 0))                            # 1. Drive A
        opened(m, n, "Drive A's window")
        sc.shot(m, "1-drive-a", out, log, mo)
        mo.dblclick(*su.zone(m, 1))                            # 2. Drive B
        opened(m, n + 1, "Drive B's window")
        sc.shot(m, "2-drive-b", out, log, mo)

        b = [w for w in su.windows(m) if w.visible][-1]  # the newest is B:
        b = sc.zorder(m)[-1]
        b = [w for w in su.windows(m) if w.i == b][0]
        mo.dblclick(*row(ui, b, "APPS"))                       # 3. into APPS
        os88marty.until(m, lambda _: [w for w in su.windows(m) if w.i == b.i]
                        [0].title.upper().startswith("APPS"),
                        "the window to navigate into APPS", poll=0.1, limit=60)
        os88marty.settle(m)
        sc.shot(m, "3-apps", out, log, mo)

        # 4. drag some windows: move APPS down-left, so both are reachable
        p = sc.titlebar(m, [w for w in su.windows(m) if w.i == b.i][0])
        if p:
            sc.pdrag(mo, p[0], p[1], p[0] - 30, p[1] + 40)
        sc.shot(m, "4-dragged", out, log, mo)

        # 5. raise Drive A (its title is the FOLDER, so find it by z-order:
        #    the one that is not APPS)
        a = [w for w in su.windows(m) if w.visible and w.i != b.i][0]
        sc.pclick(mo, *su.tile(m, a))
        sc.shot(m, "5-raise-a", out, log, mo)

        # 6. double-click README.TXT in Drive A -> Note Pad
        a = [w for w in su.windows(m) if w.visible and w.i == a.i][0]
        mo.dblclick(*row(ui, a, "README.TXT"))
        try:                            # the launch, then its first paint
            os88marty.until(m, lambda _: named(m, "NOTE") or named(m, "README"),
                            "Note Pad", poll=0.1, limit=60)
            os88marty.settle(m)
        except os88marty.MartyError:
            pass                        # ...reported below
        np = named(m, "NOTE") or named(m, "README")
        sc.shot(m, "6-notepad", out, log, mo)
        if np is None:
            print("!! Note Pad did not open off README.TXT")
            m.quit()
            return

        # 7. drag Note Pad MORE THAN ONCE - the reporter's critical step
        for k, (dx, dy) in enumerate(((-40, 25), (30, -20), (25, 30))):
            n = [w for w in su.windows(m) if w.visible and w.i == np.i]
            if not n:
                break
            p = sc.titlebar(m, n[0])
            if p is None:
                break
            sc.pdrag(mo, p[0], p[1], p[0] + dx, p[1] + dy)
            sc.shot(m, "7-np-drag%d" % (k + 1), out, log, mo)

        # 8. call the APPS window to the front - THE REPORTED STEP. (The first
        # description of this said Drive A and was corrected: raising Drive A
        # comes back clean, raising APPS is the one that breaks.)
        b2 = [w for w in su.windows(m) if w.visible and w.i == b.i]
        if b2:
            sc.pclick(mo, *su.tile(m, b2[0]))
        os88marty.settle(m)
        sc.shot(m, "8-raise-apps", out, log, mo)

        # 9. ...and Drive A after it, which is reported as clean
        a2 = [w for w in su.windows(m) if w.visible and w.i == a.i]
        if a2:
            sc.pclick(mo, *su.tile(m, a2[0]))
        os88marty.settle(m)
        sc.shot(m, "9-raise-a", out, log, mo)
        m.quit()
    print("\n%d step(s) captured. Render them with:"
          "\n  python3 tools/rawdiff.py show %s" % (len(log), out))


if __name__ == "__main__":
    main()
