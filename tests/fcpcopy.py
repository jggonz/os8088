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

It never writes the shipped apps image - and it writes NO FIXED PATH either
(docs/WRITING-TESTS.md 5.5). `os88marty.launch` already boots every floppy
from a copy in the instance's own run directory, so the paste lands there; the
one file this script writes itself, the flushed volume the host-side checks
read, is keyed to the process and swept on a pass. It used to copy the apps
image to `build/fcpcopy/apps-scratch.img` first and flush to
`build/fcpcopy/after.img`, both shared by every copy of the row - and BOTH
ARMS. `fcpcopy` and `fcpsmall` run in parallel lanes of a soak, so one arm's
copyfile could land between the other's and its launch: kern_small then booted
the 1.44MB apps disk, which its mount rule 10 refuses for its nine FAT
sectors, and the row failed in `open_drive` with B:'s window at its root and
FS_MOK = 0 - "front is 'Disk' (1, 0, 0); [fm_vinst] = 8419", reproduced
exactly by booting kern_small with build/apps.img. A copy caught mid-write
gives the same picture on either kernel (a truncated B: answers int 13h 80h).
"""
import os
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88fixture                                       # noqa: E402
import os88build
import os88marty
import os88mouse
import os88sym
import dispcp
import os88disk
import os88fat
from os88geom import FERR_FULL                           # noqa: E402

S = os88sym.linear
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
# The images, overridable so this row can be pointed at a SECOND kernel.
# kern_small carries Cut/Copy/Paste as an on-demand module (SPEC.md 22.3,
# docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2), so the engine this script drives is
# read off the disk there rather than being resident - which is exactly the
# arm nothing else exercises. Pair it with $OS88_BUILD and $OS88_DEFINES, the
# knobs os88sym already has, or the symbol map will be the wrong kernel's:
#   OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk \
#   OS88_SYSIMG=build/small.img python3 tests/fcpcopy.py
SYS_IMG = os88build.at(os.environ.get("OS88_SYSIMG", "build/os8088.img"))
SRC_APPS = os88build.at(os.environ.get("OS88_APPSIMG", "build/apps.img"))
                                # THE RUN'S TREE (tests/unit/t_artpath.py):
                                # the fcpsmall arm overrides both to the small
                                # pair
OUT = os.path.abspath(os88build.at(os.path.join("build", "fcpcopy")))
                                # ...and a path this row WRITES is its own to
                                # resolve (docs/WRITING-TESTS.md 5.5, row 62)
KERNEL_SEG = 0x0060
MB_ENTSZ, MB_SEG, MB_XL, MB_XR = 12, 10, 6, 8
BAR_Y = 8
ITEM0_Y, ITEM_H = 28, 16        # SPEC.md 12: MENU_ITEM_H = 16 in a 19px bar
I_CUT, I_COPY, I_PASTE = 0, 1, 2
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
    """Pick an Edit item - and for PASTE, wait for the OPERATION, not the glass.

    A paste runs inside the menu body with the gfx lock held (SPEC.md 22.3),
    so for its whole length the screen is MORE still than when it is done, and
    `settle` returns in the middle of it: measured, [fcp_busy] = 1 when settle
    came back two host seconds into the folder paste, which then ran ~38 more
    GUEST seconds. Everything this script did next was a read of a disk in
    flight. The listing check missed MEDIA/ in SYSTEM (the window is reloaded
    when the paste ends), and the `m.flush` landed inside a file's HELD
    WRITE_SEQ stream - whose new clusters are allocated and linked to each
    other but deliberately NOT to the file until the close (SPEC.md 18.4.9) -
    so `--verify` reported them as LOST. That is 18.4.9's crash-consistent
    state, not a leak: the same run waited out reads 0 lost clusters. It is
    also why only kern_big ever showed it - kern_small's copy commits every
    chunk as a plain append, so a mid-copy snapshot of it is clean.

    So the wait is on the engine's own flag, [fcp_busy], on the GUEST clock
    (os88marty.until, SPEC.md 22.3's "running or suspended"). No paste here
    can suspend on a question - every target is new - so 1 for longer than
    the budget is a stuck copy and says so.
    """
    x = edit_cell(m)
    mo.menu(x, BAR_Y, x + 20, ITEM0_Y + item * ITEM_H)
    os88marty.settle(m)
    if item == I_PASTE:
        os88marty.until(m, lambda mm: mm.read(S("fcp_busy"), 1)[0] == 0,
                        "the paste to finish ([fcp_busy] = 0)",
                        guest=300.0, poll=0.5)
        os88marty.settle(m)     # ...and the listing it reloads on the way out


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


def host_tree(path, folders):
    """{relative name: bytes} under B:\\<folders...>, read off the IMAGE by
    tools/os88fat.py's FAT12 reader - recursively, folders included as None.

    A copy that wrote the wrong bytes, or the right bytes short, passes the
    listing check and `--verify` both: the names are there and every chain is
    reachable. This is what compares what the engine WROTE."""
    v = os88fat.Fat12(path)
    csz = v.spc * v.bps

    def raw(off_list):
        for o in off_list:
            for i in range(0, csz, 32):
                yield bytes(v.img[o + i:o + i + 32])

    def body(e):
        first, size = u16(e, 26), int.from_bytes(e[28:32], "little")
        return b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                        for c in v.chain(first))[:size] if first else b""

    def walk(ents, pre, out):
        for e in ents:
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x08 or e[:1] == b".":
                continue
            n = pre + v.pretty(e[:11])
            if e[11] & 0x10:
                out[n] = None
                walk(raw(v.cluster_off(c) for c in v.chain(u16(e, 26))),
                     n + "\\", out)
            else:
                out[n] = body(e)
        return out

    ents = [e for _, _, e in v.entries()]
    for f in folders:
        e = [x for x in ents if v.pretty(x[:11]) == f and x[11] & 0x10]
        if not e:
            return None
        ents = list(raw(v.cluster_off(c) for c in v.chain(u16(e[0], 26))))
    return walk(ents, "", {})


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
        # THE DISKS, NOT THE TARGET. `make small` builds every kern_small
        # artefact; these two are what this arm opens, and naming them is what
        # `Row(wants=...)` can carry - so the runner builds them before any row
        # starts and os88fixture.need does nothing here. That is what lets the
        # row drop builds=True. build/small.img's own prerequisites are what
        # put build/smallk/ there, which is where the symbols come from.
        os88fixture.need("build/small.img", "build/smallapps.img")
    os.makedirs(OUT, exist_ok=True)
    # NO SCRATCH COPY: launch() clones SRC_APPS into this instance's private
    # run directory, and the paste writes that clone - never the shipped
    # image, and never a file another copy of this row is cloning at the time
    with os88marty.launch(SYS_IMG, apps=SRC_APPS, machine=MACHINE) as m:
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
        # THE CLIPBOARD, fcp_cb*: fcp_op/fcp_name are the OPERATION record,
        # which fcp_paste fills from this one and which reads 0/'' until then
        say("Copy: [fcp_cbop]=%d [fcp_cbname]=%r"
            % (m.read(S("fcp_cbop"), 1)[0],
               m.read(S("fcp_cbname"), 13).split(b"\0")[0].decode("latin1")))
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

        out = os.path.join(OUT, "after-%d.img" % os.getpid())
        m.flush(1, out)

    # --- 3. ...and the volume it left behind, read by something else -------
    print("  --- the volume, walked by tools/os88disk.py --verify ---")
    try:                        # verify EXITS on a refusal rather than
        bad = os88disk.verify(out)  # returning, which ended this script
    except SystemExit as e:     # there with every other FAIL unprinted
        bad = e.code or 1
    if bad:
        fails.append("os88disk --verify refused the volume after the copies - "
                     "the guest's own listing cannot see this class of damage, "
                     "because it is drawn from the structures that are wrong")

    # --- 4. ...and what the copies HOLD, byte for byte ----------------------
    src = host_tree(out, ["MEDIA"])
    dst = host_tree(out, ["SYSTEM", "MEDIA"])
    top = host_tree(out, [])
    if src is None or dst is None:
        fails.append("the host reader cannot find B:\\MEDIA and "
                     "B:\\SYSTEM\\MEDIA on the flushed image")
    else:
        if top.get(f) != src.get(f):
            fails.append("B:\\%s is not byte-identical to B:\\MEDIA\\%s" % (f, f))
        for n in sorted(set(src) | set(dst)):
            if src.get(n, b"?") != dst.get(n, b"!"):
                fails.append("B:\\SYSTEM\\MEDIA\\%s differs from its source "
                             "(%s bytes against %s)" % (
                                 n, "-" if dst.get(n) is None else len(dst[n]),
                                 "-" if src.get(n) is None else len(src[n])))
        say("host side: %d entries under B:\\MEDIA, %d bytes, copied byte for "
            "byte" % (len(src), sum(len(b) for b in src.values() if b)))

    if fails:
        print("\nfcpcopy: %d FAILED (the volume is kept: %s)"
              % (len(fails), out))
        for x in fails:
            print("  FAIL: " + x)
        return 1
    os.remove(out)              # per-PID, so nothing else will reclaim it
    print("\nfcpcopy: the copy engine moved a file and a folder tree, and left "
          "a sound volume - PASS on %s" % MACHINE)
    return 0


sys.exit(main())
