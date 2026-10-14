#!/usr/bin/env python3
"""SPEC.md 107.3: the Desk Calendar's TASKS - typed, checked, cleared and
deleted - and a check box that redraws its MARK and nothing else.

    make && python3 tests/deskcaltask.py

The SHIPPED 360KB system disk copied to a scratch file, on MartyPC's 5150
with a Hercules card: TALL layout, and a 1bpp framebuffer the harness can
read whole (`vram`), which is what the pixel assertion needs. The state
file is flushed off the emulated drive and parsed by tools/os88dia.py.

  A  `K` and three lines - `Buy milk`, `Call plumber`, `Post letter` - are
     three tasks in the state file, in the order typed, none done; Calendar
     > Clear Done Tasks is GREYED `Clear Done (none done)` (SPEC.md 47).
  B  a click on Buy milk's BOX checks it: the screen differs from before in
     the box's 6x6 mark and in NOTHING ELSE - not the label, not the frame,
     not another row (os88ui_chkhit -> os88ui_chkmark, SPEC.md 13.15.2) - and
     after the debounce the file has it DONE. Clear Done Tasks is live.
  C  a click on Post letter's TEXT selects it, and Space checks it: the same
     mark-only picture, and DONE in the file.
  D  Calendar > Clear Done Tasks leaves `Call plumber` alone in the file,
     the item greyed again.
  E  Call plumber selected, Del, and Yes: the file has no task.

What "nothing else" means is asserted twice, because a picture cannot say
it alone: a redraw of the label lays the same pixels again and changes none
of them. So the PIXELS say nothing else changed, and a breakpoint COUNTER on
the package's own `dc_run` - every run it draws goes through it, found in a
NASM map of the very image that is running - says no text was drawn at all,
with a selection click, two runs, as the counter's own control.

**BREAK IT ON PURPOSE**: make dc_tclick redraw the whole row after the
toggle (call dc_trow) and B's counter reads 1; take the DONE flip out of
dc_tclick and B's file check fails.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88dia     # noqa: E402
from deskcaledit import Cal, TITLE   # noqa: E402

BDIR = os.path.join(HERE, "..", os.environ.get("OS88_BUILD", "build"))
MACHINE = "os8088_5150_herc_gla"
T0, TPITCH = 176, 12            # the TALL task rows (SPEC.md 107.3's table)
TASK_DONE = os88dia.TASK_DONE

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def diff(a, b, win):
    """The pixels that differ INSIDE the window's frame - the menu bar's
    clock changes on its own minute, which is not the calendar's draw."""
    out = set()
    for y in range(win.y, win.y + win.h):
        if a[y] != b[y]:
            for x in range(win.x, win.x + win.w):
                if a[y][x] != b[y][x]:
                    out.add((x, y))
    return out


def pkg_sym(tmp, name):
    """The offset of one of the PACKAGE's labels in its image: deskcal.asm
    assembled again through a wrapper that asks NASM for a symbol map - and
    refused unless that assembly is byte for byte build/deskcal.bin, so the
    offset is the running package's."""
    wrap = os.path.join(tmp, "dcwrap.asm")
    mp, out = os.path.join(tmp, "dc.map"), os.path.join(tmp, "dcwrap.bin")
    open(wrap, "w").write('[map symbols %s]\n%%include "deskcal.asm"\n' % mp)
    root = os.path.join(HERE, "..")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/deskcal/", "-o", out, wrap], check=True,
                   cwd=root)
    if open(out, "rb").read() != open(os.path.join(BDIR, "deskcal.bin"),
                                      "rb").read():
        raise SystemExit("deskcal.asm does not assemble to build/deskcal.bin"
                         " - `make` first")
    for line in open(mp):
        f = line.split()
        if len(f) == 3 and f[2] == name and re.match(r"^[0-9A-Fa-f]+$", f[0]):
            return int(f[0], 16)
    raise SystemExit("no %s in the map" % name)


def bbox(px):
    if not px:
        return ""
    xs = [p[0] for p in px]
    ys = [p[1] for p in px]
    return ", in (%d,%d)-(%d,%d)" % (min(xs), min(ys), max(xs), max(ys))


def main():
    tmp = tempfile.mkdtemp(prefix="deskcaltask-")
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    try:
        with os88ui.boot(img, machine=MACHINE) as ui:
            c = Cal(ui, tmp, check=check)
            m = c.m
            run_at = c.wseg * 16 + pkg_sym(tmp, "dc_run")

            def tasks():
                return [(t["text"], t["done"])
                        for t in (c.saved("the save") or {}).get("tasks", [])]

            def clear_item():
                c.front()
                cal = [mm for mm in ui.menus() if mm[0] == "Calendar"]
                its = cal[0][3] if cal else []
                return its[4] if len(its) > 4 else None

            def box(k):
                ox, oy = c.origin()
                return ox, oy + T0 + TPITCH * k

            def mark(k):
                x, y = box(k)
                return set((x + 3 + i, y + 3 + j) for i in range(6)
                           for j in range(6))

            def frame_now():
                ui.mo.to(20, 300)        # the pointer off the window: it is
                os88marty.pace(m, 1.0)   # drawn INTO the framebuffer
                return m.vram("herc")[2]

            print("A: three tasks typed")
            for line in ("Buy milk", "Call plumber", "Post letter"):
                c.key("KeyK")
                c.type(line)
                c.key("Enter")
                c.field_shut("the field to shut after %r" % line)
            got = tasks()
            check(got == [("Buy milk", False), ("Call plumber", False),
                          ("Post letter", False)],
                  "the file holds the three, in order, none done (%r)"
                  % (got,))
            check(clear_item() == ("Clear Done (none done)", False),
                  "Clear Done Tasks is greyed with its reason (%r)"
                  % (clear_item(),))

            print("B: Buy milk's box: the mark and nothing else")
            x, y = box(0)
            c.front()
            a = frame_now()
            check(all(a[py][px] for px, py in mark(0)),
                  "the box is empty before: its mark's pixels are ground")
            runs = os88marty.bp_count(m, run_at,
                                      lambda: ui.mo.click(x + 5, y + 5))
            check(runs == 0, "the toggle drew NO text run (%d of the "
                  "package's dc_run)" % runs)
            ui._wait(lambda: c.bss(os88dia.DESKCAL_DIA_BSS + 16
                                   + os88dia.DESKCAL_TASK0)[0] & TASK_DONE,
                     "Buy milk to be DONE in memory", 30)
            b = frame_now()
            d = diff(a, b, ui.window(TITLE))
            check(d == mark(0), "only the 6x6 mark changed in the window (%d pixels, %d of "
                  "them outside the mark%s)" % (len(d), len(d - mark(0)),
                                                bbox(d - mark(0))))
            check(not any(b[py][px] for px, py in mark(0)),
                  "...and it is the solid ink mark")
            got = tasks()
            check(got[:1] == [("Buy milk", True)], "the file has it DONE "
                  "(%r)" % (got,))
            check(clear_item() == ("Clear Done Tasks", True),
                  "Clear Done Tasks is live now (%r)" % (clear_item(),))

            print("C: Post letter selected by its text, then Space")
            for k in (1, 2):
                c.front()
                runs = os88marty.bp_count(
                    m, run_at, lambda: ui.mo.click(x + 100,
                                                   y + k * TPITCH + 5))
                check(runs == 2, "...selecting row %d is two runs, the old "
                      "row's text and its own - the counter's control (%d)"
                      % (k, runs))
            os88marty.pace(m, 1.0)
            check(c.bss(os88dia.DESKCAL_BSS_SELK)[0] == 2
                  and c.bss(os88dia.DESKCAL_BSS_SELI)[0] == 2,
                  "task 2 is selected")
            a = frame_now()
            c.front()
            runs = os88marty.bp_count(m, run_at,
                                      lambda: m.key("Space"))
            check(runs == 0, "Space drew no text run (%d)" % runs)
            ui._wait(lambda: c.bss(os88dia.DESKCAL_DIA_BSS + 16
                                   + os88dia.DESKCAL_TASK0
                                   + 2 * os88dia.DESKCAL_TASK)[0] & TASK_DONE,
                     "Post letter to be DONE in memory", 30)
            b = frame_now()
            d = diff(a, b, ui.window(TITLE))
            check(d == mark(2), "Space drew its mark and nothing else (%d "
                  "pixels, %d outside)" % (len(d), len(d - mark(2))))
            got = tasks()
            check(got == [("Buy milk", True), ("Call plumber", False),
                          ("Post letter", True)], "the file (%r)" % (got,))

            print("D: Clear Done Tasks")
            c.front()
            ui.menu_pick("Calendar", "Clear Done Tasks")
            got = tasks()
            check(got == [("Call plumber", False)],
                  "Call plumber is the one left (%r)" % (got,))
            check(clear_item() == ("Clear Done (none done)", False),
                  "...and the item is greyed again")

            print("E: Call plumber deleted, after asking")
            c.front()
            ui.mo.click(x + 100, y + 5)
            os88marty.pace(m, 1.0)
            c.key("Delete")
            ui._wait(lambda: len([w for w in ui.windows()
                                  if w.title == TITLE]) == 2,
                     "the Delete question", 30)
            c.answer_yes("the Delete question")
            got = tasks()
            check(got == [], "no task is left in the file (%r)" % (got,))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcaltask: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
