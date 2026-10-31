#!/usr/bin/env python3
"""win16fam - S17's program families, each opened, looked at and closed
(SPEC.md 124.22, docs/plans/WIN16-ANY-PLAN.md S17).

    python3 tests/win16fam.py                 VGA and CGA (row win16fam)
    python3 tests/win16fam.py --video ega     one of vga, ega, cga
    python3 tests/win16fam.py --only CHARMAP.EXE
    python3 tests/win16fam.py --keep          leave the last QEMU for a look

*As a user, Notepad, Cardfile, Calendar, Character Map, Clipboard and PIF
Editor run* - and the Task Manager and the 3.1 screen savers' setup dialogs,
the story's other targets. Microsoft's files are never in this tree; the
win16-reference skill copies them into build/win16/ (the savers' .SCR as
SS*.EXE), and a program that is not there is skipped. None of them at all
is a SKIP.

One boot per adapter, each program in turn, the one before closed first
(two instances at once are short of memory, SPEC.md 124.18.2):

  1. WIN16 says Running and a window opens whose title has the family's
     word in it;
  2. its client is not one colour - something was drawn in it. The Clipboard
     viewer is empty by design and is spared this;
  3. Character Map's window fits the screen: its template is a DS_SETFONT
     dialog (SPEC.md 124.22.17), and in the System font's units it was 696
     pixels across, its buttons past the right edge;
  4. it ends: a main window by its close box, a dialog (the Task List, a
     saver's setup) by Esc.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
from win16mines import b                                      # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16sol import (SDrive, system_for, glass, region,      # noqa: E402
                      window_of, W16S_REFUSED, W16S_RUNNING, W16S_ENDED)

# name, a word its title holds, ended by Esc (a dialog) rather than the box
FAMILY = (
    ("NOTEPAD.EXE", "Notepad", False),
    ("CARDFILE.EXE", "Cardfile", False),
    ("CALENDAR.EXE", "Calendar", False),
    ("CLIPBRD.EXE", "Clipboard", False),
    ("PIFEDIT.EXE", "PIF", False),
    ("TASKMAN.EXE", "Task", True),
    ("CHARMAP.EXE", "Character", False),
    ("SSSTARS.EXE", "Starfield", True),
    ("SSMARQUE.EXE", "Marquee", True),
    ("SSMYST.EXE", "Mystify", True),
    ("SSFLYWIN.EXE", "Flying", True),
)
BLANK_OK = ("CLIPBRD.EXE",)
VIDEOS = ("vga", "cga")


def progs(only):
    have = []
    for row in FAMILY:
        if only and row[0] not in only:
            continue
        if os.path.exists(b(os.path.join("win16", row[0]))):
            have.append(row)
        else:
            print("  (no build/win16/%s: skipped)" % row[0])
    return have


def run_one(d, ui, S, dw, row, video, work, bad):
    name, word, esc = row
    tag = "%s %s" % (video.upper(), name)
    from win16cpu import raise_
    raise_(d, ui, dw)
    p = F.Prog(d, ui, d.x.sym, S, name)
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or p.text("w16_l2") == "Running", 300, "%s to run" % name)
    d.idle(6.0)
    st = p.pb("w16_state")
    if st != W16S_RUNNING:
        bad.append("%s: WIN16 %s it: %s / %s" % (tag, "refused" if st == W16S_REFUSED
                   else "ended", p.text("w16_l1"), p.text("w16_l2")))
        return
    w = window_of(ui, p)
    title = w.title if w else ""
    print("%s: running, window %r" % (tag, title))
    if word.lower() not in title.lower():
        bad.append("%s: the window is %r, not one with %r in it" % (tag, title, word))
    if w is None:
        return
    cx0, cy0, cx1, cy1 = w.content
    vw, vh, rows = glass(d, video, work)
    if name not in BLANK_OK:
        flat = [v for r in region(rows, cx0, cy0, min(cx1, vw), min(cy1, vh))
                for v in r]
        felt = max(set(flat), key=flat.count) if flat else None
        other = sum(1 for v in flat if v != felt) / float(len(flat) or 1)
        print("   %.1f%% of the client is not its commonest colour" % (100 * other))
        if other < 0.005:
            bad.append("%s: the client is one colour - nothing drawn" % tag)
    if name == "CHARMAP.EXE":
        print("   window x %d..%d on a %d-pixel screen" % (w.x, w.x + w.w, vw))
        if w.x + w.w > vw:
            bad.append("%s: the window reaches x %d, past the screen's %d - "
                       "the dialog face did not lay it out" % (tag, w.x + w.w, vw))
    if esc:
        d.key("esc")
    else:
        d.click(*geom.close_xy(w.x, w.y, w.w, m=d, sym=d.x.sym))
    d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
           "%s to end" % name)
    d.idle(4.0)


def leg(video, rows, keep, bad):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16m-")     # short: a QMP socket path
    try:
        sysimg = system_for(video)
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [r[0] for r in rows])
        d = SDrive(work, sysimg, bimg, keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        for r in rows:
            run_one(d, ui, S, dw, r, video, work, bad)
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", choices=("vga", "ega", "cga"))
    ap.add_argument("--only", action="append")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    rows = progs(a.only)
    if not rows:
        print("win16fam: SKIP - none of S17's programs is in build/win16 "
              "(the win16-reference skill)")
        return 77
    bad = []
    vids = [a.video] if a.video else VIDEOS
    for v in vids:
        leg(v, rows, a.keep, bad)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16fam: %d programs ran, drew and ended on %s"
              % (len(rows), ", ".join(v.upper() for v in vids)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
