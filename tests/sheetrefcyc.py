#!/usr/bin/env python3
"""Formula > Reference, Excel's own meaning (SPEC.md 81.108).

    make && python3 tests/sheetrefcyc.py

The Reference Guide: "Converts the selected references in the formula bar
from relative to absolute, from absolute to mixed, and from mixed back to
relative ... if the insertion point is within or next to the reference.
Shortcut key: F4."  A1 > $A$1 > A$1 > $A1 > A1.

On the cycle-accurate CGA, through the real keyboard, read out of SHEET's own
entry buffer (sh_editbuf) - the text the field shows and Enter commits:

  1. outside an entry the item is GREYED and F4 does nothing
  2. "=A1+B2" with the caret after B2: F4 four times walks B2 round the
     whole cycle and back, and A1 is never touched
  3. the caret moved INTO A1: F4 converts A1, not B2
  4. the MENU does it too, mid-entry: Formula > Reference is live then
  5. "=SUM(A1)" with the caret after SUM: F4 changes nothing - a function's
     name is not a reference
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetrefcyc"
DISK = "build/sheetrefcyc.img"
FORMULA = (123 + 24 + 36, 45)           # sheetundo's Edit centre, + half
                                        # of Edit's cell and half of Formula's
REF_ROW = 2                             # SH_FI_REF


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0, (1, 1): 2.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL",
                    "APPS:" + src], check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        S = lambda n: m.sym(n)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        w, h, rows = m.vram("cga")
        ys, xs = glass.grid(w, h, rows)
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def buf():
            return bytes(m.readseg(seg, sym["sh_editbuf"], 32)) \
                .split(b"\0")[0].decode("latin-1")

        def greyed():
            v = m.readseg(seg, sym["sh_i_formula"] + 2 * REF_ROW, 2)
            return m.readseg(seg, v[0] | (v[1] << 8), 1)[0] == 1

        def key(name, n=1):
            for _ in range(n):
                m.key(name)
                M.guest_sleep(m, 0.3)
            M.settle(m)

        def typed(text):
            for ch in text:
                m.type_text(ch)
                M.guest_sleep(m, 0.3)
            M.settle(m)

        # 1: nothing being entered
        mo.click(*at(3, 3))
        M.settle(m)
        key("F4")
        got["idle"] = (m.readseg(seg, sym["sh_editing"], 1)[0], buf()[:1])
        mo.to(*FORMULA)                     # open Formula: sh_refmark runs
        mo._edge(True)
        M.settle(m)
        got["idle_grey"] = greyed()
        mo.to(634, 190, l=True)
        mo._edge(False)
        M.settle(m)

        # 2: =A1+B2, caret after B2
        typed("=A1+B2")
        got["typed"] = buf()
        cyc = []
        for _ in range(4):
            key("F4")
            cyc.append(buf())
        got["cycle"] = cyc

        # 3: into A1 - Left past "+B2" and one more puts the caret after A1
        key("ArrowLeft", 3)
        key("F4")
        got["a1"] = buf()

        # 4: the menu, mid-entry
        mo.to(*FORMULA)
        mo._edge(True)
        M.settle(m)
        got["live"] = not greyed()
        mo.to(FORMULA[0] + 17, 57 + 12 * REF_ROW + 2, l=True)
        mo._edge(False)
        M.settle(m)
        got["menu"] = buf()
        _, _, r = m.vram("cga")
        M.write_png(os.path.join(WORK, "menu.png"), 640, 200, r)
        key("Escape")

        # 5: a function's name is not a reference
        mo.click(*at(2, 3))                 # a CGA shows four rows
        M.settle(m)
        typed("=SUM(A1)")
        key("ArrowLeft", 5)                 # caret right after SUM
        key("F4")
        got["sum"] = buf()
        key("Escape")

    print("  ", got)
    check(got["idle"][0] == 0 and got["idle_grey"],
          "outside an entry F4 does nothing and the item is greyed",
          "sh_refmark: live only mid-formula, in A1 style",
          got=(got["idle"], got["idle_grey"]))
    check(got["typed"] == "=A1+B2", "the formula was typed",
          got=got["typed"], want="=A1+B2")
    want = ["=A1+$B$2", "=A1+B$2", "=A1+$B2", "=A1+B2"]
    check(got["cycle"] == want,
          "F4 walks the reference at the caret round Excel's cycle",
          "A1 > $A$1 > A$1 > $A1 > A1, and A1 is left alone",
          got=got["cycle"], want=want)
    check(got["a1"] == "=$A$1+B2", "with the caret in A1, F4 converts A1",
          got=got["a1"], want="=$A$1+B2")
    check(got["live"] and got["menu"] == "=A$1+B2",
          "Formula > Reference is live mid-entry and does the same",
          "the menu path fires the same item", got=(got["live"], got["menu"]),
          want=(True, "=A$1+B2"))
    check(got["sum"] == "=SUM(A1)",
          "a function's name is not a reference: SUM is left alone",
          got=got["sum"], want="=SUM(A1)")
    done("sheetrefcyc")


if __name__ == "__main__":
    main()
