#!/usr/bin/env python3
"""Cut/Copy/Paste actually moves a file and a folder tree (SPEC.md 22.3-22.5).

    make && python3 tests/fcpcopy.py [machine]

WHY THIS EXISTS.  Before it, NOTHING in tests/ exercised kernel/filecp.inc -
grepping `fcp_`, "Paste", 22.3 and 22.5 across tests/ and tests/suite.py
returned the copy engine's name exactly nowhere.  A whole-file pass over that
module can therefore be green on assembly, on `stkbalance`, on `os88ovlchk`
and on every size guard while leaving a machine that cannot copy a file, and
none of those checks is even looking.

It drives the real surface - a click to select, Edit > Copy, navigate,
Edit > Paste - and then asserts three things, of which only the first is
visible on the glass:

  1. [fcp_err] is FERR_OK and the copy is in the destination's LISTING.
  2. A copied FOLDER holds what the source folder held.  That is the arm that
     walks fcp_scan / fcp_mkroot / fcp_mksub / fcp_push / fcp_frame /
     fcp_faddr / fcp_relink, none of which a plain file touches.
  3. The volume the engine LEFT BEHIND is walked by tools/os88disk.py's own
     FAT12 reader.  This is the assertion that matters: a copy engine that
     goes wrong strands clusters, cross-links two chains or writes a
     directory entry pointing at nothing, and every one of those looks
     perfectly fine in the guest's own listing - which is drawn from the same
     structures that are wrong.  `verify` counts the chains independently.

THE DISK MUST HAVE ROOM, and running out is not a failure of the kernel: on
the 360KB apps disk this same script gets FERR_FULL (6) part-way through the
folder copy, because that geometry ships 354 of 354 clusters in use once one
more file has been pasted.  That is SPEC.md 22.5.2 working - fcp_room asks
before anything is created, fcp_undo removes the partial destination, and
`--verify` still passes on the volume afterwards.  So it runs on the 1.44MB
disk, and FERR_FULL is reported as the harness's choice of disk rather than
as a defect.

It writes to a COPY of the apps image, never the shipped one: the paste is a
real write and the next test to boot that image would see it.
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp
import os88disk

S = os88sym.linear
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
# The images, overridable so this row can be pointed at a SECOND kernel.
# kern_small carries Cut/Copy/Paste as an on-demand module (SPEC.md 22.3,
# docs/KERN-SMALL-MODULE-SPLIT.md 9.2), so the engine this script drives is
# read off the disk there rather than being resident - which is exactly the
# arm nothing else exercises. Pair it with $OS88_BUILD and $OS88_DEFINES, the
# knobs os88sym already has, or the symbol map will be the wrong kernel's:
#   OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk \
#   OS88_SYSIMG=build/small.img python3 tests/fcpcopy.py
SYS_IMG = os.environ.get("OS88_SYSIMG", "build/os8088.img")
SRC_APPS = os.environ.get("OS88_APPSIMG", "build/apps.img")
OUT = os.path.abspath(os.path.join("build", "fcpcopy"))
KERNEL_SEG = 0x0060
MB_ENTSZ, MB_SEG, MB_XL, MB_XR = 12, 10, 6, 8
BAR_Y = 8
ITEM0_Y, ITEM_H = 28, 16        # SPEC.md 12: MENU_ITEM_H = 16 in a 19px bar
I_CUT, I_COPY, I_PASTE = 0, 1, 2
FERR_FULL = 6
fails = []


def say(s):
    print("  " + s)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def edit_cell(m):
    """The Edit menu's bar cell, READ OUT OF menu_bar rather than guessed.

    The cells are laid out by menu_bar_build from the title STRINGS, so they
    move whenever one of them changes length - and a click one cell over opens
    a different menu and then picks whatever sorted into that item index,
    which is a command running silently instead of an error.
    """
    for cell in range(8):
        t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
        p, sg = u16(t, 0), u16(t, MB_SEG)
        if not p:
            continue
        if m.read((sg or KERNEL_SEG) * 16 + p, 16).split(b"\0")[0] == b"Edit":
            return (u16(t, MB_XL) + u16(t, MB_XR)) // 2
    sys.exit("fcpcopy: no 'Edit' cell in menu_bar - the Locator's menus have "
             "moved and this harness is aiming at nothing")


def pick_edit(m, mo, item):
    x = edit_cell(m)
    mo.menu(x, BAR_Y, x + 20, ITEM0_Y + item * ITEM_H)
    os88marty.settle(m)


def select(m, mo, wx, wy, name):
    """One CLICK on a row - the SELECTION Cut/Copy act on, not an open."""
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                           dispcp.row_of(m, S, name))
    mo.click(*dispcp.row_xy(wx, wy, row))
    os88marty.settle(m)
    say("selected %s at row %d" % (name, row))


def err(m):
    return m.read(S("fcp_err"), 1)[0]


def names(m):
    return [n for n, _ in dispcp.listing(m, S) if n != ".."]


def goto_root(m, mo, wx, wy):
    while ".." in [n for n, _ in dispcp.listing(m, S)]:
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")


def main():
    # POINTED AT kern_small, BUILD ITS IMAGE FIRST - smallboot.py's shape and
    # for its reason: `all` never builds that kernel, and there is no
    # capability to probe for, so a row that needed the disk to be lying
    # about would simply never run. `make small` is idempotent and builds
    # into build/smallk/, so it disturbs nothing in the default tree.
    if "smallk" in os.environ.get("OS88_BUILD", ""):
        subprocess.check_call(["make", "small"],
                              stdout=subprocess.DEVNULL)
    os.makedirs(OUT, exist_ok=True)
    apps = os.path.join(OUT, "apps-scratch.img")
    shutil.copyfile(SRC_APPS, apps)      # NEVER the shipped image
    with os88marty.launch(SYS_IMG, apps=apps, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        goto_root(m, mo, wx, wy)
        say("B:\\ = %r" % names(m))

        # --- 1. a PLAIN FILE: MEDIA/ -> the root ---------------------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
        inner = names(m)
        say("B:\\MEDIA = %r" % inner)
        f = inner[0]
        select(m, mo, wx, wy, f)
        pick_edit(m, mo, I_COPY)
        say("Copy: [fcp_op]=%d [fcp_name]=%r"
            % (m.read(S("fcp_op"), 1)[0],
               m.read(S("fcp_name"), 13).split(b"\0")[0].decode("latin1")))
        goto_root(m, mo, wx, wy)
        pick_edit(m, mo, I_PASTE)
        e = err(m)
        say("Paste of %s into B:\\ -> [fcp_err]=%d" % (f, e))
        if e:
            fails.append("the file paste reported FERR %d" % e)
        if f not in names(m):
            fails.append("%s is not in B:\\ after the paste - it lists %r"
                         % (f, names(m)))
        else:
            say("B:\\ now holds %s" % f)

        # --- 2. a FOLDER: MEDIA/ -> B:\SYSTEM ------------------------------
        select(m, mo, wx, wy, "MEDIA")
        pick_edit(m, mo, I_COPY)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "SYSTEM")
        pick_edit(m, mo, I_PASTE)
        e = err(m)
        say("Paste of MEDIA/ into B:\\SYSTEM -> [fcp_err]=%d" % e)
        if e == FERR_FULL:
            fails.append("the folder paste hit FERR_FULL: this disk has no "
                         "room, which is the HARNESS's choice and not a "
                         "defect - see the docstring")
        elif e:
            fails.append("the folder paste reported FERR %d" % e)
        if "MEDIA" not in names(m):
            fails.append("MEDIA/ is not in B:\\SYSTEM after the paste - %r"
                         % names(m))
        else:
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
            got = names(m)
            say("B:\\SYSTEM\\MEDIA = %r (the source held %r)" % (got, inner))
            if sorted(got) != sorted(inner):
                fails.append("the copied folder holds %r, the source held %r"
                             % (sorted(got), sorted(inner)))

        out = os.path.join(OUT, "after.img")
        m.flush(1, out)

    # --- 3. ...and the volume it left behind, read by something else -------
    print("  --- the volume, walked by tools/os88disk.py --verify ---")
    if os88disk.verify(out):
        fails.append("os88disk --verify refused the volume after the copies - "
                     "the guest's own listing cannot see this class of damage, "
                     "because it is drawn from the structures that are wrong")

    if fails:
        print("\nfcpcopy: %d FAILED" % len(fails))
        for x in fails:
            print("  FAIL: " + x)
        return 1
    print("\nfcpcopy: the copy engine moved a file and a folder tree, and left "
          "a sound volume - PASS on %s" % MACHINE)
    return 0


sys.exit(main())
