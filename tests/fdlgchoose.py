#!/usr/bin/env python3
"""The Standard File chooser, end to end (SPEC.md 38).

The chooser is a Disk window in a chooser role (SPEC.md 38.1), so this row
drives what is the CHOOSER's and not the Disk window's, through Note Pad's own
File > Open and File > Save As, and confirms every step off guest state:

  1. a first Open lands on MEDIA (38.10), captioned 'Open', with the button
     column's Open GREYED until a row is selected (38.3, SPEC.md 47);
  2. a click selects and Enter answers; with nothing selected Down selects
     nothing, and on kern_big Up and Down then MOVE the selection - SPEC.md
     22.26's, the Disk window's own, which the chooser inherits (38.4);
  3. Save As puts the app's document in the box, on kern_big Down fills it
     from the next row (FDH_SEL), a typed name commits, and the file is in
     the folder afterwards;
  4. Escape, the Cancel button and the close box each cancel, and the next
     Open remembers the folder (38.10) - and presses queued in the SAME drain
     as the close box's release are swallowed, so a drive icon's double-click
     there cannot launch a Disk window into the dead chooser's slot and be
     adopted as the chooser (38.2) - and in a folder that PAGES, Down with
     nothing selected scrolls the chooser's own block (22.26);
  5. Drive leaves the floppy (38.11);
  6. with four of the USER's Disk windows open a fifth is refused and the
     chooser still opens - the fifth pool block is its own (38.1).

Run against kern_small (where the glue is FDLG.DRV, SPEC.md 38.0) with
os88sym's own knobs and the small system disk, which carries the apps:

    OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk \\
    OS88_SYSIMG=build/small360.img OS88_NP=A:/APPS/NOTEPAD.O88 \\
        python3 tests/fdlgchoose.py

VERIFIED TO FAIL: `make NOFDMEDIA=1` reds step 1 (the chooser opens on
B:\\APPS, where Note Pad was launched from). Step 4's same-drain leg reds on
the kernel before SPEC.md 38.2's "between the end and the reap" rule: the
drive's double-click opens A: in that drain.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402

SYS = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
APPS = os.environ.get("OS88_APPSIMG", "build/apps360.img")
NP = os.environ.get("OS88_NP", "B:/APPS/NOTEPAD.O88")
fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def blk(ui):
    return (geom.KERNEL_SEG << 4) + ui._word("fdlg_blk")


def fs_sel(ui):
    return int.from_bytes(ui.m.read(blk(ui) + geom.FS_SEL, 2), "little")


def ebuf(ui):
    raw = bytes(ui.m.read(ui._S("fm_ebuf"), 13))
    return raw.split(b"\0")[0].decode("ascii", "replace")


def inject(ui, recs):
    """Queue mouse records in the guest's event ring as ONE drain's worth.

    The machine is stopped at the top of ui_task's event section, where no
    evq_* critical section is in flight, and the records go in behind
    whatever is queued - so the pass that resumes pops them all, in order,
    before its tail runs. That is the only way to put two gestures in one
    drain on purpose: a real mouse spaces them by the UART and the clock.
    """
    m = ui.m
    m.bp_exec(ui._S("ui_task.events"))
    m.run()
    if not m.wait_stop(60.0):
        raise RuntimeError("ui_task never reached its event section")
    m.bp_exec()
    t = ui._word("ticks")
    tail = ui._byte("evq_tail")
    n = ui._byte("evq_count")
    assert n + len(recs) <= 16, "the ring has %d queued" % n
    buf = ui._S("evq_buf")
    for ty, x, y in recs:
        m.write(buf + tail, b"".join(v.to_bytes(2, "little")
                                     for v in (ty, x, y, t)))
        tail = (tail + 8) & 0x7F
        n += 1
    m.write(ui._S("evq_tail"), bytes([tail]))
    m.write(ui._S("evq_count"), bytes([n]))
    m.run()


def ink(ui, w, k):
    """Dark pixels inside column button k - a greyed caption is dithered."""
    x, y = ui.chooser_button_xy(k, w)
    W, H, px = ui.m.fbuf()
    n = 0
    for yy in range(y - 4, y + 4):
        for xx in range(x - 24, x + 24):
            n += px[(yy * W + xx) * 3] < 0x40
    return n


with os88ui.boot(SYS, apps=APPS) as ui:
    m = ui.m
    ui.path(NP)
    ui.settle()                 # a menu picked while the app is still
                                # coming up is lost, on the base tree too
    print("== the Standard File chooser (%s) ==" % SYS)

    # --- 1: first Open: MEDIA, 'Open', the default button greyed --------------
    ui.menu_pick("File", "Open")
    w = ui.chooser()
    ui.settle()                     # the column is the paint's LAST part
    rows = [r[0] for r in ui.listing(w)]
    check("captioned Open", w.title == "Open", "(%r)" % w.title)
    check("lands on MEDIA (38.10)", "GUIDE.TEX" in rows, "(%r)" % rows)
    grey = ink(ui, w, ui.CH_OPEN)

    # --- 2: a click selects, Enter answers; the arrows only scroll ----------
    m.key("ArrowDown")
    ui.settle()
    check("Down with nothing selected selects nothing (22.26)",
          fs_sel(ui) == 0xFFFF, "(FS_SEL %04X)" % fs_sel(ui))
    want = ui.chooser_select("GUIDE.TEX", w)
    ui.settle()
    live = ink(ui, w, ui.CH_OPEN)
    check("Open goes live on a selection (47)", live > grey,
          "(ink %d -> %d)" % (grey, live))
    check("a click selects the row", fs_sel(ui) == want, "(%d)" % fs_sel(ui))
    if "KERN_SMALL" not in os.environ.get("OS88_DEFINES", ""):
        m.key("ArrowUp")
        ui.settle()
        check("Up moves the selection (22.26)", fs_sel(ui) == want - 1,
              "(%d)" % fs_sel(ui))
        m.key("ArrowDown")
        ui.settle()
        check("...and Down brings it back", fs_sel(ui) == want,
              "(%d)" % fs_sel(ui))
    m.key("Enter")
    ui.chooser_gone()
    check("Enter on a file answers the Open", True)

    # --- 3: Save As -----------------------------------------------------------
    ui.menu_pick("File", "Save As")
    w = ui.chooser()
    check("captioned Save As", w.title == "Save As", "(%r)" % w.title)
    check("the box holds the document", ebuf(ui) == "GUIDE.TEX",
          "(%r)" % ebuf(ui))
    if "KERN_SMALL" not in os.environ.get("OS88_DEFINES", ""):
        # a FILE row with a file below it: the click fills the box (38.4),
        # and Down moves the selection, whose FDH_SEL fills it again (22.26)
        ls = ui.listing(w)
        i = next((k for k in range(len(ls) - 1)
                  if ls[k][1] not in (2, 3) and ls[k + 1][1] not in (2, 3)),
                 None)
        if i is None:
            sys.exit("FAILURES: MEDIA has no two files in a row to walk "
                     "(%r)" % ls)
        ui.chooser_select(ls[i][0], w)
        check("a click on a file fills the box", ebuf(ui) == ls[i][0],
              "(%r)" % ebuf(ui))
        m.key("ArrowDown")
        try:
            ui._wait(lambda: ebuf(ui) == ls[i + 1][0], "Down to fill the box",
                     os88ui.T_NAV)
        except os88ui.UIError:
            pass
        check("Down fills the box from the next row (22.26)",
              ebuf(ui) == ls[i + 1][0] and fs_sel(ui) == i + 1,
              "(%r, FS_SEL %d)" % (ebuf(ui), fs_sel(ui)))
    ui.chooser_save("NEWNOTE.TXT")
    check("Save As commits a typed name", True)

    # --- 4: three cancels, and the folder is remembered -----------------------
    for how in ("escape", "button", "close"):
        ui.menu_pick("File", "Open")
        w = ui.chooser()
        rows = [r[0] for r in ui.listing(w)]
        if how == "escape":
            check("the save landed in the folder", "NEWNOTE.TXT" in rows,
                  "(%r)" % rows)
        ui.chooser_cancel(how)
        check("cancelled by %s" % how, True)

    # --- 4a: with nothing selected, Down scrolls a folder that PAGES ---------
    # MEDIA has fewer rows than fit, so step 2's Down cannot scroll there;
    # APPS beside it pages on both builds
    ui.menu_pick("File", "Open")
    w = ui.chooser()
    ui.open("..", expect="nav", win=w)
    ui.open("APPS", expect="nav", win=w)
    ui.settle()
    n, fit, s0 = len(ui.listing(w)), ui._word("fm_fit"), ui.scroll(w)
    check("APPS pages in the chooser", n > fit, "(%d rows, %d fit)"
          % (n, fit))
    m.key("ArrowDown")
    try:
        ui._wait(lambda: ui.scroll(w) != s0, "the chooser to scroll",
                 os88ui.T_NAV)
    except os88ui.UIError:
        pass
    check("Down with nothing selected scrolls (22.26)",
          ui.scroll(w) == s0 + 1 and fs_sel(ui) == 0xFFFF,
          "(FS_SCRL %d -> %d, FS_SEL %04X)" % (s0, ui.scroll(w), fs_sel(ui)))
    ui.chooser_cancel("escape")

    # --- 4b: the close box, and a drive double-click in the SAME drain -------
    # Between the release that closes the chooser and the reap at the pass's
    # tail, the chooser's record, pool block and window slot are free, and a
    # Disk window launched there takes them - and was adopted as the chooser
    # (SPEC.md 38.2). With the rule, every press in that drain is swallowed.
    ui.menu_pick("File", "Open")
    w = ui._refresh(ui.chooser())
    cx, cy = geom.close_xy(w.x, w.y)
    dx, dy = geom.drive_pt(m, "A", ui.sym)
    over = [o.title for o in ui.windows()
            if o.visible and o.i != w.i and o.covers(dx, dy)]
    check("drive A:'s zone is on the glass", not over, "(%r)" % over)
    before = sorted(o.i for o in ui.windows() if o.i != w.i)
    inject(ui, [(1, cx, cy), (2, cx, cy),          # EVT_MDOWN, EVT_MUP
                (1, dx, dy), (2, dx, dy), (1, dx, dy), (2, dx, dy)])
    try:
        ui.chooser_gone()
    except os88ui.UIError:
        pass                        # ...the checks below say what is up
    ui.settle()
    after = sorted(o.i for o in ui.windows())
    check("the chooser is reaped (38.2)", ui._word("fdlg_win") == 0
          and ui._word("fdlg_blk") == 0,
          "(fdlg_win %04X)" % ui._word("fdlg_win"))
    check("the same drain's double-click is swallowed", after == before,
          "(%r -> %r: %r)" % (before, after, ui.titles()))
    if ui._word("fdlg_win"):
        sys.exit("FAILURES: a Disk window was adopted as the chooser - "
                 "nothing below can run behind it (%r)" % fails)
    for o in ui.windows():
        if o.i not in before:
            ui.close(o)             # ...so step 6 counts what it always did

    # --- 5: Drive leaves the floppy -------------------------------------------
    ui.menu_pick("File", "Open")
    w = ui.chooser()
    drv0 = m.read(blk(ui) + geom.FS_DRV, 1)[0]
    ui.chooser_button(ui.CH_DRIVE, w)
    ui.settle()
    drv1 = m.read(blk(ui) + geom.FS_DRV, 1)[0]
    check("Drive moves to another volume (38.11)", drv1 != drv0,
          "(%d -> %d)" % (drv0, drv1))
    ui.chooser_cancel("button")

    # --- 6: four of the user's Disk windows, and the chooser still opens -----
    for _ in range(4):
        # BY NAME: [fm_vinst] still names the chooser that just closed, as
        # it names any Disk window that closed last - disk_window() is the
        # ACTING window and there is none until one is raised
        ui.raise_window(ui.window("APPS"))
        ui.menu_pick("Nav", "New Window")
        ui.settle()
    disks = [t for t in ui.titles() if t != "Note Pad"]
    check("the user holds four Disk windows, and no more",
          len(disks) == 4, "(%d)" % len(disks))
    ui.raise_window(ui.window("Note Pad"))
    ui.menu_pick("File", "Open")
    w = ui.chooser()
    check("the chooser opens as the fifth (38.1)", w is not None)
    ui.chooser_cancel("escape")

print()
if fails:
    print("FAILURES:")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("all pass")
