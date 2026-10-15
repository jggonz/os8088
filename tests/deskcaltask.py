#!/usr/bin/env python3
"""SPEC.md 110.3: the Desk Calendar's TASKS - typed, checked, cleared and
deleted - and a check box whose toggle is the BOX ALONE, one sub-rect BLIT1.

    make && python3 tests/deskcaltask.py

The SHIPPED 360KB system disk copied to a scratch file, on MartyPC's 5150
with a Hercules card: v2's MID layout (348 rows, SPEC.md 110.3), and a 1bpp
framebuffer the harness can read whole (`vram`). The state file is flushed
off the emulated drive and parsed by tools/os88dia.py; every coordinate is
the layout's, out of tools/os88dia.py's DESKCAL_GEO, which t_dia holds to
the package's own dc_geos.

  A  `K` and three lines - `Buy milk`, `Call plumber`, `Post letter` - are
     three tasks in the state file, in the order typed, none done; Calendar
     > Clear Done Tasks is GREYED `Clear Done (none done)` (SPEC.md 47).
  B  THE BOX: 9 x 9 at face x 8, its frame ink and its inside ground. A
     press on Buy milk's box checks it, and the gesture is EXACTLY ONE
     drawing call of the calendar's - an OSAPI_GFX_BLIT1 (a breakpoint on
     every drawing cell, counted when its far return is the calendar's
     segment, tests/deskcalperf.py's instrument) - and the screen differs
     from before inside the box's rect and NOWHERE ELSE; the box is then
     pixel for pixel the TICKED record (2-pixel strokes, SPEC.md
     13.16.2.1's note). After the debounce the file has it DONE, and Clear
     Done Tasks is live.
  C  the list PAGES (two lines, three tasks: `+2 more`): a press on the
     pager shows Call plumber, a press on its TEXT selects it, and Space
     checks it - one BLIT1 again - and DONE in the file.
  D  Calendar > Clear Done Tasks leaves `Post letter` alone in the file, the
     item greyed again.
  E  Post letter selected, Del, and Yes: the file has no task.
  F  three tasks again: the list shows the page holding the last one typed,
     and its pager wraps back to the first page.

**BREAK IT ON PURPOSE**: make dc_toggle redraw card 2 (dc_rday) instead of
the box and B counts 3 BLIT1s; take the DONE flip out and B's file check
fails; draw the plain record for a done task and B's tick compare fails.
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
TASK_DONE = os88dia.TASK_DONE
BOX9 = (0xFF80, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080,
        0xFF80)                 # dcdraw.inc's dc_box9, and its ticked twin
TICK9 = (0xFF80, 0x8080, 0x8380, 0x8680, 0xAC80, 0xB880, 0x9080, 0x8080,
         0xFF80)

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


def rec_bad(img, x, y, rec, w):
    """The pixels of a w-wide bit record at (x, y) that are not what the
    record says (ink where a bit is set, ground where it is clear)."""
    bad = []
    for r, bits in enumerate(rec):
        for c in range(w):
            ink = bool(bits & (0x8000 >> c))
            if (not img[y + r][x + c]) != ink:
                bad.append((x + c, y + r))
    return bad


def main():
    from deskcalperf import measure, Gesture
    tmp = tempfile.mkdtemp(prefix="deskcaltask-")
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    try:
        with os88ui.boot(img, machine=MACHINE) as ui:
            c = Cal(ui, tmp, check=check)
            m = c.m
            check(c.li() == 1, "Hercules takes the MID layout (dia_li %d)"
                  % c.li())
            g = c.g()

            def g_tag():
                return os88dia.DESKCAL_GEO[c.li()]["tag"]

            def tasks():
                return [(t["text"], t["done"])
                        for t in (c.saved("the save") or {}).get("tasks", [])]

            def clear_item():
                c.front()
                cal = [mm for mm in ui.menus() if mm[0] == "Calendar"]
                its = cal[0][3] if cal else []
                return its[4] if len(its) > 4 else None

            def box(k):
                return c.at(1, 8, g["T0"] + g["TP"] * k)

            def frame_now():
                ui.mo.to(20, 300)        # the pointer off the window: it is
                os88marty.pace(m, 1.0)   # drawn INTO the framebuffer
                return m.vram("herc")[2]

            def done(k):
                return c.bss(os88dia.DESKCAL_DIA_BSS + 16
                             + os88dia.DESKCAL_TASK0
                             + k * os88dia.DESKCAL_TASK)[0] & TASK_DONE

            def toggled(what, act, k):
                a = frame_now()
                c.front()
                hits, _ = measure(m, lambda: (act(),
                                              os88marty.guest_sleep(m, 1.5)))
                # a PRESS reveals the handle at once (SPEC.md 110.11) - the
                # include's one tag-row BLIT1, there when the handle was not
                # up - so the tag row's calls are set aside: the CALENDAR's
                # draw is what is counted
                w = c.win()
                tag_y = w.y + w.h - g_tag()
                hits = [h for h in hits
                        if not (h.get("regs") and h["regs"]["bx"] >= tag_y)]
                gst = Gesture(hits, c.wseg)
                ui._wait(lambda: done(k), "task %d to be DONE in memory" % k,
                         30)
                b = frame_now()
                x, y = box(0)
                rect = set((x + i, y + j) for i in range(g["BOX"])
                           for j in range(g["BOX"]))
                d = diff(a, b, c.win())
                check(gst.n == 1 and gst.by == {"GFX_BLIT1": 1},
                      "%s: the toggle is ONE sub-rect BLIT1 (%s)" % (what, gst))
                check(d and d <= rect, "%s: only the box's rect changed (%d "
                      "pixels, %d outside it%s)" % (what, len(d),
                                                    len(d - rect),
                                                    bbox(d - rect)))
                bad = rec_bad(b, x, y, TICK9, 9)
                check(not bad, "%s: the box is the TICKED record, every pixel "
                      "(%d off)" % (what, len(bad)))

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

            print("B: Buy milk's box: the box, one BLIT1")
            ttop = os88dia.DESKCAL_BSS_TTOP
            while c.bss(ttop)[0] != 0:          # the page holding task 0
                c.press(c.task_text(g["TN"] - 1))
            a = frame_now()
            x, y = box(0)
            bad = rec_bad(a, x, y, BOX9, 9)
            check(not bad, "the box is 9 x 9 at face x 8: its frame ink, its "
                  "inside ground (%d off)" % len(bad))
            toggled("the box", lambda: ui.mo.click(*c.task_box(0)), 0)
            got = tasks()
            check(got[:1] == [("Buy milk", True)], "the file has it DONE "
                  "(%r)" % (got,))
            check(clear_item() == ("Clear Done Tasks", True),
                  "Clear Done Tasks is live now (%r)" % (clear_item(),))

            print("C: paged to Call plumber, selected by its text, Space")
            c.front()
            c.press(c.task_text(g["TN"] - 1))       # `+2 more`
            check(c.bss(ttop)[0] == 1, "the pager showed the next page "
                  "(dc_ttop %d)" % c.bss(ttop)[0])
            c.press(c.task_text(0))
            check(c.bss(os88dia.DESKCAL_BSS_SELK)[0] == 2
                  and c.bss(os88dia.DESKCAL_BSS_SELI)[0] == 1,
                  "task 1 is selected")
            toggled("Space", lambda: m.key("Space"), 1)
            got = tasks()
            check(got == [("Buy milk", True), ("Call plumber", True),
                          ("Post letter", False)], "the file (%r)" % (got,))

            print("D: Clear Done Tasks")
            c.front()
            ui.menu_pick("Calendar", "Clear Done Tasks")
            got = tasks()
            check(got == [("Post letter", False)],
                  "Post letter is the one left (%r)" % (got,))
            check(clear_item() == ("Clear Done (none done)", False),
                  "...and the item is greyed again")

            print("E: Post letter deleted, after asking")
            c.front()
            c.press(c.task_text(0))
            c.key("Delete")
            ui._wait(lambda: len([w for w in ui.windows()
                                  if w.title == TITLE]) == 2,
                     "the Delete question", 30)
            c.answer_yes("the Delete question")
            got = tasks()
            check(got == [], "no task is left in the file (%r)" % (got,))

            print("F: three again: the last one's page, and the wrap")
            for line in ("T0", "T1", "T2"):
                c.key("KeyK")
                c.type(line)
                c.key("Enter")
                c.field_shut("the field to shut after %r" % line)
            check(c.bss(ttop)[0] == 2, "the list shows T2's page (dc_ttop %d)"
                  % c.bss(ttop)[0])
            c.press(c.task_text(g["TN"] - 1))
            check(c.bss(ttop)[0] == 0, "...and its pager wraps to the first "
                  "(dc_ttop %d)" % c.bss(ttop)[0])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcaltask: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
