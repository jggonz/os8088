#!/usr/bin/env python3
"""A REUSED WINDOW SLOT AND A STALE BUTTON RECORD (SPEC.md 20.5.1.3.3).

Sheet's five dialogs each own a STATIC os88ui button record, re-inited at
every open, and each is destroyed on close - so the next dialog gets the same
window slot, and so the same window pointer. os88ui_btnclick answers a press
with the FIRST record on the package's list whose OS88UI_BT_WIN is the window
pressed. Linking a record once (the fix for the list that looped on itself)
leaves a re-inited record where it is, so the closed dialog's record - still
naming that slot - can sit AHEAD of the live one and take its clicks.

The sequence that showed it, driven on Sheet's real dialogs:

  1. Formula > Goto...    (the Goto dialog, slot k)     Cancel
  2. Format  > Number...  (the Format dialog, slot k)   Cancel
  3. Formula > Goto...    (slot k again)

- through Sheet's own in-window bar, which the system menu_pick cannot
reach - and then two checks on the third open:

  a. the FIRST record on the list naming the window is the Goto dialog's own
     (read off the list, the walk os88ui_btnclick does)
  b. a click on its Cancel CLOSES it - [sh_idlg_win] goes back to 0. With the
     stale record ahead, the press arms the Format record's Cancel instead,
     and the Goto dialog stays open.

Steps 1 and 2 must land on the SAME slot or the row proves nothing; it says
so and fails rather than passing on a sequence that never reused a slot.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT + "/tools")
import os88ui, os88marty, os88geom

BT_RECTS, BT_WIN, BT_NEXT = 0, 8, 14    # apps/os88ui.inc's OS88UI_BT_*


def u16(b): return b[0] | (b[1] << 8)


def sheet_syms():
    """Sheet's own symbols, off a re-assembly with a map (btngesture's trick)."""
    src = ROOT + "/apps/sheet/sheet.asm"
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "t.asm"), os.path.join(d, "t.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + INC + ["-I", ROOT + "/apps/",
                        "-I", ROOT + "/apps/sheet/",
                        "-o", os.path.join(d, "t.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


# A NEGATIVE CONTROL names its own disk and the include directory its Sheet
# was built with (an os88ui.inc to read ahead of apps/), so the map the row
# reads is that build's and not this tree's
APPS = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/build/office360.img"
INC = ["-I", sys.argv[2].rstrip("/") + "/"] if len(sys.argv) > 2 else []
S = sheet_syms()

with os88ui.boot(ROOT + "/build/os8088-360.img", apps=APPS,
                 machine="os8088_xt_vga") as ui:
    m, mo = ui.m, ui.mo
    w = ui.path("B:/SHEET.O88")
    seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
    ui.settle()
    rd = lambda sym: u16(m.readseg(seg, S[sym], 2))

    def cancel(rec):
        """Click the dialog's second button (Cancel) through its record."""
        rects = u16(m.readseg(seg, S[rec] + BT_RECTS, 2))
        x0, y0, x1, y1 = (u16(m.readseg(seg, rects + 8 + i * 2, 2))
                          for i in range(4))
        mo.click((x0 + x1) // 2, (y0 + y1) // 2)
        os88marty.settle(m)
        ui.settle()

    SH_MPAD, SH_MI_H = 8, 12                 # apps/sheet/sheet.asm

    def pick(menu, item):
        """Sheet's OWN in-window bar (sh_mtrack): press the title, drag onto
        the item, release - every coordinate read off Sheet's state."""
        x = rd("sh_ox")
        for i in range(menu):
            x += u16(m.readseg(seg, S["sh_mw"] + i * 2, 2)) + SH_MPAD * 2
        mo.to(x + SH_MPAD + 2, rd("sh_oy") + 6)
        os88marty.settle(m)
        mo._edge(True); m.advance(frames=12); m.run()
        mo.to(rd("sh_mrx1") + 12, rd("sh_mry1") + 2 + item * SH_MI_H + 6,
              l=True)
        m.advance(frames=12); m.run()
        mo._edge(False)
        os88marty.settle(m)
        ui.settle()

    GOTO, NUMBER = (2, 5), (3, 0)            # Formula > Goto..., Format > Number...

    pick(*GOTO)
    k1 = rd("sh_idlg_win")
    cancel("sh_idlg_btrec")
    gone1 = rd("sh_idlg_win")

    pick(*NUMBER)
    k2 = rd("sh_fdlg_win")
    cancel("sh_fdlg_btrec")
    gone2 = rd("sh_fdlg_win")

    pick(*GOTO)
    k3 = rd("sh_idlg_win")

    first, r, walked = 0, rd("os88ui_btlist"), 0
    while r and walked < 16:
        if u16(m.readseg(seg, r + BT_WIN, 2)) == k3:
            first = r
            break
        r = u16(m.readseg(seg, r + BT_NEXT, 2))
        walked += 1
    cancel("sh_idlg_btrec")
    after = rd("sh_idlg_win")

    names = {S["sh_idlg_btrec"]: "Goto's", S["sh_fdlg_btrec"]: "Format's"}
    res = [
        ("Goto opened, then closed", k1 != 0 and gone1 == 0, "win %04x -> %04x" % (k1, gone1)),
        ("Format opened, then closed", k2 != 0 and gone2 == 0, "win %04x -> %04x" % (k2, gone2)),
        ("...on the SAME slot", k1 == k2 == k3, "%04x %04x %04x" % (k1, k2, k3)),
        ("first record naming it is Goto's", first == S["sh_idlg_btrec"],
         names.get(first, "%04x" % first)),
        ("its Cancel closes it", after == 0, "win %04x after the click" % after),
    ]
    ok = True
    for i, (what, good, got) in enumerate(res, 1):
        ok &= good
        print("%d. %-34s %-28s %s" % (i, what, got, "PASS" if good else "FAIL"))
    print("\n%s" % ("ALL PASS" if ok else "*** FAILED ***"))
    sys.exit(0 if ok else 1)
