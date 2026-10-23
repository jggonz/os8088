#!/usr/bin/env python3
"""DOES SHEET ASK BEFORE IT THROWS A WORKBOOK AWAY? (SPEC.md 81.118)

    make && python3 tests/sheetask.py

Note Pad, TeXPad, Paint and Word all answer SPEC.md 75.1's close negotiation:
a document with unsaved work puts *Save changes to NAME?* up and REFUSES to
close. SHEET was the odd one out in its own system, and this is the row for
the port that fixed it.

What it knows it by is a FLAG, [sh_modified], set by the eight routines that
can change a document - the cell table's add and remove, the border and note
tables' four, Clear's per-cell arm and the format dialog's - and cleared by
New, by a successful read and by a save that kept EVERYTHING. A flag is only
as good as the places that set and clear it, so this drives all four states
and reads the byte at each:

  OPENED      0 - a document just read owes nothing, although every cell it
                made went through sh_addcell and marked it on the way in
  TYPED       1 - one entry
  SAVED       0 - File > Save, and the file really is on the disk
  TYPED AGAIN 1 - so the clear was not a stuck zero

...and then the feature itself, twice, because a question that cannot be
answered is worse than no question:

  CLOSE BOX   the window is STILL THERE, and an alert is up
  DISCARD     ...and answering it closes the window

THE LAST TWO ARE WHY THE FIRST FOUR ARE HERE. A flag stuck at 1 would pass
the close test on its own and fail nothing.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                               # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from paintmove import pkg_syms                                # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402

WORK = "build/sheetask"
DISK = "build/sheetask.img"
NAME = "ASK.SLK"
CELLS = {(0, 0): 'Item', (0, 1): 'Cost', (1, 0): 'Rope', (1, 1): 12.5}
TITLE_H = 18
ABW, ABG, ABH, ABTNY = 72, 12, 13, 46     # os88ui.inc's alert layout


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)

    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    seen, still, extra, closed, saved = [], None, None, None, None

    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        SF.open_shin(m, mo, NAME)
        M.settle(m, limit=240)
        wins0 = dispcp.win_list(m, S, check=False)
        sw = wins0[-1]
        seg = m.read(S("wm_wins") + sw * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)

        def flag():
            return m.readseg(seg, sym["sh_modified"], 1)[0]

        def note(what):
            seen.append((what, flag()))

        def park():
            mo.to(634, 180)              # off the grid: the pointer is ink
            M.settle(m)

        park()
        note("opened")

        g = glass.grid(*m.vram("cga"))
        if not g:
            check(False, "the grid is on the glass", "no grid found")
            done("sheetask")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def typed(r, c, what):
            mo.click(*at(r, c))
            M.settle(m)
            m.type_text(what + "\n")
            M.settle(m, limit=120)
            park()

        typed(2, 1, "9")
        note("typed")

        SF.menu_pick(m, mo, seg, sym, "sh_i_file", "save")
        M.settle(m, limit=240)
        for _ in range(40):
            v = os88flush.Flush(marty=m).volume(1)
            if NAME in v.names():
                d = v.read(NAME)
                if d and b"9" in d:
                    saved = d
                    break
            M.settle(m, quiet=2.0, stable=2, limit=60)
        park()
        note("saved")

        typed(3, 1, "8")
        note("typed again")

        # --- THE CLOSE BOX -------------------------------------------------
        r = dispcp.win_rect(m, S, sw)
        mo.click(r[0] + 8, r[1] + 9)
        M.settle(m, limit=240)
        after = dispcp.win_list(m, S, check=False)
        still = sw in after
        extra = len(after) - len(wins0)

        # --- ...AND ANSWERING IT. os88ui_arect's own formula: three buttons
        # of ABW with ABG between, the row centred. Button 1 is Discard.
        if still and extra >= 1:
            al = [w for w in after if w != sw and w not in wins0]
            if al:
                ax, ay, aw, _ah = dispcp.win_rect(m, S, al[-1])
                row = 3 * (ABW + ABG) - ABG
                bx = ax + (aw - row) // 2 + (ABW + ABG) + ABW // 2
                by = ay + TITLE_H + ABTNY + ABH // 2
                print("   the alert: x=%d y=%d w=%d, Discard at %d,%d"
                      % (ax, ay, aw, bx, by))
                M.write_png_rgb(os.path.join(WORK, "1-asked.png"), *m.fbuf())
                mo.click(bx, by)
                M.settle(m, limit=240)
                closed = sw not in dispcp.win_list(m, S, check=False)

    for what, d in seen:
        print("   %-14s [sh_modified] = %d" % (what, d))
    print("   the close box: SHEET %s, %s window(s) more than before"
          % ("is still open" if still else "IS GONE", extra))
    if closed is not None:
        print("   Discard:       SHEET %s"
              % ("IS STILL OPEN" if not closed else "is gone"))

    want = {"opened": 0, "typed": 1, "saved": 0, "typed again": 1}
    for what, d in seen:
        check(d == want[what], "[sh_modified] is %d after %s" % (want[what], what),
              "the flag the question rests on", got=d, want=want[what])
    check(saved is not None, "File > Save wrote the file",
          "the saved case above means nothing if nothing was written")
    check(still, "the close box does NOT close a changed workbook",
          "SPEC.md 75.1: the negotiator refuses and asks", got=still, want=True)
    check(still and extra >= 1, "...and an alert is up",
          "a refusal with no question is a window that can never be closed",
          got=extra, want=">= 1")
    check(closed is True, "Discard closes it",
          "SPEC.md 75.3: an answer that does not act leaves a window nothing "
          "can shut", got=closed, want=True)
    done("sheetask")


if __name__ == "__main__":
    main()
