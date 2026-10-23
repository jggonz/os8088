#!/usr/bin/env python3
"""A dialog's CLOSE BOX leaves its engine usable (SPEC.md 81.100).

    make && python3 tests/sheetdlgclose.py

A package's secondary window has no owner record, so the kernel's own close
only hides it (SPEC.md 75.1). SHEET's list dialog (Format > Number, Paste
Function, Paste Name) and its input dialog (Goto, Define Name, Row Height,
Column Width, Sort, Run, INPUT) each refuse to open while their window word
is set, and nothing cleared it on a close-box close - so one close box shut
the whole engine for the session, in silence.

Four arms, each read out of SHEET's own bss AND confirmed by the window
appearing (a window word alone could be set by a create that drew nothing):

  1. Format > Number, closed by its box: [sh_ldlg_win] back to 0
  2. ...and Formula > Paste Function then OPENS - the list engine lives
  3. Formula > Goto, closed by its box: [sh_idlg_win] back to 0
  4. ...and Formula > Define Name then OPENS - the input engine lives
  5. Formula > Note (81.102: its engine and os88text.inc now run from
     CHART.OVL): typed text and OK store the note, and reopening loads it
     back into the box - open, key, click, apply and load, every door
  6. ...closed by its box, [sh_noteopen] and the window word clear, and it
     opens again: the third engine 81.100.1 missed
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
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetdlgclose"
DISK = "build/sheetdlgclose.img"
NAME = "BOX.SLK"
TITLES = ["File", "Edit", "Formula", "Format", "Data", "Options", "Macro",
          "Sheets", "Help"]


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src], check=True,
                   stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine="os8088_xt_vga") as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)
        sl = dispcp.win_list(m, S, check=False)[-1]
        x0, y0, w0, h0 = dispcp.win_rect(m, S, sl)
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        X, x = {}, x0 + 1                    # sh_mtab_calc: 8*len + 16 wide
        for t in TITLES:
            X[t] = x + (8 * len(t) + 16) // 2
            x += 8 * len(t) + 16
        bar = y0 + 18 + 7
        item = lambda t, i: (X[t] + 6, y0 + 18 + 14 + 2 + 12 * i + 6)
        park = (x0 + w0 - 30, y0 + h0 // 2)

        def word(name):
            v = m.readseg(seg, sym[name], 2)
            return v[0] | (v[1] << 8)

        def top():                           # None once SHEET itself is
            w = dispcp.win_list(m, S, check=False)   # gone: a refused open
            return w[-1] if w else None      # sends the next close box to
                                             # SHEET's own window

        def close_box():
            t = top()
            if t is None or t == sl:         # nothing opened: never click
                return                       # SHEET's own close box
            bx, by, _, _ = dispcp.win_rect(m, S, t)
            mo.click(bx + 9, by + 9)
            M.settle(m, limit=60)

        def open_item(t, i):
            mo.menu(X[t], bar, *item(t, i))
            M.settle(m, limit=120)
            mo.to(*park)

        open_item("Format", 0)
        got["n_open"] = (word("sh_ldlg_win"), top() != sl)
        close_box()
        got["n_shut"] = (word("sh_ldlg_win"), top() == sl)
        open_item("Formula", 1)
        got["pf_open"] = (word("sh_ldlg_win"), top() != sl)
        close_box()
        open_item("Formula", 5)
        got["g_open"] = (word("sh_idlg_win"), top() != sl)
        close_box()
        got["g_shut"] = (word("sh_idlg_win"), top() == sl)
        open_item("Formula", 3)
        got["dn_open"] = (word("sh_idlg_win"), top() != sl)
        close_box()

        def byte(name, n=1):
            return m.readseg(seg, sym[name], n)

        open_item("Formula", 4)                  # 5: Note - type, OK
        got["nt_open"] = (word("sh_ndlg_win"), top() != sl)
        for ch in "hi":
            m.type_text(ch)
            M.guest_sleep(m, 0.3)
        M.settle(m)
        t = top()
        if t is not None and t != sl:
            nx, ny, _, _ = dispcp.win_rect(m, S, t)
            M.write_png_rgb(os.path.join(WORK, "note.png"), *m.fbuf())
            mo.click(nx + 1 + 254, ny + 18 + 34)  # OK: SH_NDLG_BTX1..2, OKY1..2
            M.settle(m)
        got["nt_ok"] = (word("sh_ndlg_win"), byte("sh_noteopen")[0])
        open_item("Formula", 4)                  # ...reopened: loaded back
        got["nt_load"] = bytes(byte("sh_notetext", 3))
        close_box()                              # 6: its close box
        got["nt_shut"] = (word("sh_ndlg_win"), byte("sh_noteopen")[0],
                          top() == sl)
        open_item("Formula", 4)
        got["nt_again"] = (word("sh_ndlg_win"), top() != sl)
        close_box()

    print("  ", got)
    check(got["n_open"][0] != 0 and got["n_open"][1],
          "Format > Number opened (the arm is not vacuous)",
          "[sh_ldlg_win] set and a window above SHEET's", got=got["n_open"])
    check(got["n_shut"] == (0, True),
          "its CLOSE BOX clears [sh_ldlg_win] and the window goes",
          "sh_ldlg_cls_r: before 81.100 the kernel only hid it and the word "
          "stayed set", got=got["n_shut"], want=(0, True))
    check(got["pf_open"][0] != 0 and got["pf_open"][1],
          "...so Formula > Paste Function then OPENS",
          "the list engine refused every open for the session before",
          got=got["pf_open"])
    check(got["g_open"][0] != 0 and got["g_open"][1],
          "Formula > Goto opened", "[sh_idlg_win] set", got=got["g_open"])
    check(got["g_shut"] == (0, True),
          "its CLOSE BOX clears [sh_idlg_win] and the window goes",
          "sh_idlg_cls_r takes it as Esc, INPUT's Cancel",
          got=got["g_shut"], want=(0, True))
    check(got["dn_open"][0] != 0 and got["dn_open"][1],
          "...so Formula > Define Name then OPENS", "the input engine lives",
          got=got["dn_open"])
    check(got["nt_open"][0] != 0 and got["nt_open"][1],
          "Formula > Note opens from CHART.OVL", "81.102's open door",
          got=got["nt_open"])
    check(got["nt_ok"] == (0, 0), "typing and OK closes it",
          "the module's key, click and apply", got=got["nt_ok"], want=(0, 0))
    check(got["nt_load"] == b"hi\0", "reopened, the box holds the note typed",
          "stored by sh_nt_set, loaded by sh_note_load through its new "
          "vector", got=got["nt_load"], want=b"hi\0")
    check(got["nt_shut"] == (0, 0, True),
          "its CLOSE BOX clears the window word and [sh_noteopen]",
          "sh_ndlg_cls_r: before 81.102 the kernel only hid it",
          got=got["nt_shut"], want=(0, 0, True))
    check(got["nt_again"][0] != 0 and got["nt_again"][1],
          "...so Formula > Note opens again", got=got["nt_again"])
    done("sheetdlgclose")


if __name__ == "__main__":
    main()
