#!/usr/bin/env python3
"""SHEET's keyboard shortcuts, the modern set (SPEC.md 81.101).

    make && python3 tests/sheetkeys.py

Ctrl+N/O/S, Ctrl+Z/X/C/V, Ctrl+R/D, F3, Shift+F3, Ctrl+G and F5, Ctrl+F and
F9. One table in sheet.asm drives the key AND the caption the pulldown draws
beside the item, and a shortcut fires the item through sh_mfire - the door a
click uses - so this drives the real keyboard on the cycle-accurate CGA and
reads what happened off the glass (tests/glass.py), off SHEET's own dialog
words, and off the saved file:

  1. the captions: the Edit pulldown shows Ctrl+Z/X/C/V/R/D on its rows,
     right-aligned, and Options shows F9 on Calculate Now
  2. Ctrl+C on A1, Ctrl+V on C1: C1 shows 1
  3. Ctrl+Z: C1 is empty again - the item's own undo snapshot
  4. Ctrl+X on B1, Ctrl+V on D1: the label moves
  5. A1:A3 selected, Ctrl+D: A2 and A3 show 1
  6. mid-entry, Ctrl+V pastes NOTHING: E1 takes the typed 7
  7. F3, Shift+F3, Ctrl+G, F5, Ctrl+F, Ctrl+N each open their dialog
  8. Ctrl+S saves: the file on the disk is what 2-6 left
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
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetkeys"
DISK = "build/sheetkeys.img"
CELLS = {(0, 0): 1.0, (1, 0): 2.0, (0, 1): 'x'}
EDIT = (123, 45)                        # sheetundo's measured titles
OPTIONS = (123 + 280, 45)
SH_MPAD, SH_MI_H = 8, 12                # sheet.asm's own
SAVED = "APPS/SHIN.SLK"                 # Save writes it IN PLACE, in APPS
WANT = {0: "Ctrl+Z", 1: "Ctrl+X", 2: "Ctrl+C", 3: "Ctrl+V", 9: "Ctrl+R",
        10: "Ctrl+D"}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL",
                    "APPS:" + src], check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    sym = pkg_syms("apps/sheet/sheet.asm")
    seen, caps, dlg = {}, {}, {}
    data = None
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        S = lambda n: m.sym(n)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        word = lambda n: (lambda v: v[0] | (v[1] << 8))(m.readseg(seg, sym[n], 2))
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetkeys")
            return
        ys, xs = g
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        park = (634, 180)

        def chord(mod, key):
            if mod:
                m.key(mod, down=True, up=False)
            m.key(key)
            if mod:
                m.key(mod, down=False, up=True)
            M.settle(m)

        ctrl = lambda k: chord("ControlLeft", "Key" + k)

        def look(tag, cells):
            mo.click(*at(3, 5))                 # F4: empty, and not read
            M.settle(m)
            mo.to(*park)
            M.settle(m)
            _, _, r = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, r)
            seen[tag] = {k: glass.cell_text(r, box(*k), table, xs, ys)
                         for k in cells}

        def captions(title, rows_):
            """hold a pulldown open and read each row's right-hand text:
            rows_ maps a row to its caption, whose length sizes the box -
            wider, and the end of the label is read with it"""
            mo.to(*title)
            mo._edge(True)
            M.settle(m)
            mo.to(*park, l=True)
            M.settle(m)
            x2, y1 = word("sh_mrx2"), word("sh_mry1")
            _, _, r = m.vram("cga")
            M.write_png(os.path.join(WORK, "menu-%d.png" % title[0]),
                        640, 200, r)
            out = {}
            for i, t in rows_.items():
                y = y1 + 2 + SH_MI_H * i
                out[i] = glass.cell_text(r, (x2 - SH_MPAD - 8 * len(t) - 1, y,
                                             x2 - SH_MPAD + 1, y + SH_MI_H),
                                         table)
            mo._edge(False)
            M.settle(m)
            return out

        caps["options"] = captions(OPTIONS, {4: "F9"})

        mo.click(*at(0, 0))                     # 2: Ctrl+C, Ctrl+V
        M.settle(m)
        ctrl("C")
        mo.click(*at(0, 2))
        M.settle(m)
        ctrl("V")
        look("2-pasted", [(0, 2)])
        # the Edit captions NOW: Undo is live after a paste, and a greyed
        # row's caption is greyed with it - which the glass cannot read
        caps["edit"] = captions(EDIT, WANT)
        mo.click(*at(0, 2))                     # 3: Ctrl+Z
        M.settle(m)
        ctrl("Z")
        look("3-undone", [(0, 2)])
        mo.click(*at(0, 1))                     # 4: Ctrl+X, Ctrl+V
        M.settle(m)
        ctrl("X")
        mo.click(*at(0, 3))
        M.settle(m)
        ctrl("V")
        look("4-moved", [(0, 1), (0, 3)])
        mo.click(*at(0, 0))                     # 5: A1:A3, Ctrl+D
        M.settle(m)
        m.key("ShiftLeft", down=True, up=False)
        mo.click(*at(2, 0))
        m.key("ShiftLeft", down=False, up=True)
        M.settle(m)
        ctrl("D")
        look("5-filled", [(1, 0), (2, 0)])
        mo.click(*at(0, 4))                     # 6: mid-entry Ctrl+V
        M.settle(m)
        m.type_text("7")
        M.settle(m)
        ctrl("V")
        dlg["editing"] = word("sh_editing") & 0xFF
        m.key("Enter")
        M.settle(m)
        look("6-typed", [(0, 4)])

        # 7: each dialog shortcut opens its dialog; Esc or its box shuts it
        def opens(tag, press, wword):
            press()
            M.settle(m, limit=120)
            dlg[tag] = word(wword)
            top = dispcp.win_list(m, S, check=False)[-1]
            if top != sl:
                bx, by, _, _ = dispcp.win_rect(m, S, top)
                mo.click(bx + 9, by + 9)
                M.settle(m)
                mo.click(*at(3, 5))             # the radio kinds only hide:
                M.settle(m)                     # a grid click recovers them

        opens("F3", lambda: chord(None, "F3"), "sh_ldlg_win")
        opens("Shift+F3", lambda: chord("ShiftLeft", "F3"), "sh_ldlg_win")
        opens("Ctrl+G", lambda: ctrl("G"), "sh_idlg_win")
        opens("F5", lambda: chord(None, "F5"), "sh_idlg_win")
        opens("Ctrl+F", lambda: ctrl("F"), "sh_idlg_win")

        # CTRL+N IS THE ONE THAT DOES NOT OPEN A DIALOG HERE, and that is the
        # feature rather than the shortcut failing: steps 2-6 changed the
        # document, so File > New asks before it throws the work away
        # (81.118). What the accelerator has to prove is that it reaches
        # File > New at all - the ALERT is that proof - so this counts the
        # window and cancels it, and the New dialog itself is driven after
        # Ctrl+S below, where the document is clean again.
        nwin = len(dispcp.win_list(m, S, check=False))
        ctrl("N")
        M.settle(m, limit=120)
        after = dispcp.win_list(m, S, check=False)
        dlg["Ctrl+N asked"] = len(after) - nwin
        dlg["Ctrl+N fdlg"] = word("sh_fdlg_win")
        if len(after) > nwin:                   # Cancel: button 2 of 3
            ax_, ay_, aw_, _ah = dispcp.win_rect(m, S, after[-1])
            row = 3 * (72 + 12) - 12
            mo.click(ax_ + (aw_ - row) // 2 + 2 * (72 + 12) + 36,
                     ay_ + 18 + 46 + 6)
            M.settle(m, limit=120)

        before = open(os.path.join(WORK, "SHIN.SLK"), "rb").read()
        ctrl("S")                               # 8: Save, in place
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if SAVED in v.names() and v.read(SAVED) != before:
                data = v.read(SAVED)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

        # ...and NOW the document is clean, so the same accelerator opens the
        # dialog it always did. The pair is the point: either reading alone
        # would pass with the ask wired the wrong way round.
        opens("Ctrl+N clean", lambda: ctrl("N"), "sh_fdlg_win")

    print("   captions", caps, "dialogs", dlg)
    for i, t in WANT.items():
        got = (caps["edit"].get(i) or "").strip()
        check(got == t, "Edit's row %d shows %s, right-aligned" % (i, t),
              "the caption comes from the same table the key does",
              got=got, want=t)
    got = (caps["options"].get(4) or "").strip()
    check(got == "F9", "Options' Calculate Now shows F9", got=got, want="F9")
    s = lambda tag, k: seen.get(tag, {}).get(k)
    check(s("2-pasted", (0, 2)) == '1', "Ctrl+C then Ctrl+V copies A1 to C1",
          got=s("2-pasted", (0, 2)), want='1')
    check(s("2-pasted", (0, 2)) == '1' and s("3-undone", (0, 2)) is None,
          "Ctrl+Z undoes the paste",
          "sh_mfire: the item's own undo snapshot. Tied to the paste having "
          "happened, or an empty C1 would pass it with no key wired at all",
          got=(s("2-pasted", (0, 2)), s("3-undone", (0, 2))), want=('1', None))
    check(s("4-moved", (0, 1)) is None and s("4-moved", (0, 3)) == 'x',
          "Ctrl+X then Ctrl+V moves B1's label to D1",
          got=(s("4-moved", (0, 1)), s("4-moved", (0, 3))), want=(None, 'x'))
    check(s("5-filled", (1, 0)) == '1' and s("5-filled", (2, 0)) == '1',
          "Ctrl+D fills A1 down A2:A3",
          got=(s("5-filled", (1, 0)), s("5-filled", (2, 0))))
    check(dlg.get("editing") == 1 and s("6-typed", (0, 4)) == '7',
          "mid-entry, Ctrl+V pastes nothing: the entry stays open and E1 "
          "takes the typed 7", got=(dlg.get("editing"), s("6-typed", (0, 4))),
          want=(1, '7'))
    for tag in ("F3", "Shift+F3", "Ctrl+G", "F5", "Ctrl+F"):
        check(dlg.get(tag, 0) != 0, "%s opens its dialog" % tag,
              "the engine's window word is set", got=dlg.get(tag))
    check(dlg.get("Ctrl+N asked") == 1 and dlg.get("Ctrl+N fdlg") == 0,
          "Ctrl+N on a CHANGED document asks instead (81.118)",
          "the accelerator reaches File > New, and New asks before it throws "
          "the work away - one window more, and the New dialog NOT open",
          got=(dlg.get("Ctrl+N asked"), dlg.get("Ctrl+N fdlg")), want=(1, 0))
    check(dlg.get("Ctrl+N clean", 0) != 0,
          "...and once Ctrl+S has saved it, Ctrl+N opens New",
          "the engine's window word is set", got=dlg.get("Ctrl+N clean"))
    got = F.read_sylk(data) if data else {}
    want = {(0, 0): 1.0, (1, 0): 1.0, (2, 0): 1.0, (0, 3): 'x', (0, 4): 7.0}
    bad = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
    extra = [k for k in got if k not in want]
    check(data is not None and not bad and not extra,
          "Ctrl+S saves in place: the file is exactly what the keys left",
          "differs at %r, extra %r" % (bad, extra))
    done("sheetkeys")


if __name__ == "__main__":
    main()
