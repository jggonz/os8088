#!/usr/bin/env python3
"""The BIOS-did-not-set-DL check, both ways (SPEC.md 2.9.11).

    python3 tests/dljunk.py

A Packard Bell 286 (86Box `pb286`, BIOS 09/17/86) jumps to the boot sector with
`DL = 0x61` - not a drive number, just whatever was in the register. Stage 1
believed it, every int 13h named a unit that is not there, and the machine
printed `Disk error`. It had done that since the first commit of this
repository (docs/FIELD-NOTES.md 36); SPEC.md 2.9.11's seven-byte range check is
the fix.

NOTHING IN THIS TREE HAS SUCH A ROM. Every BIOS here sets DL, and MartyPC
cannot run a 286 at all, so the machine that shows the bug cannot be hosted -
which is why `DLJUNK=<n>` exists: it overwrites DL immediately before the check,
so `DLJUNK=0x61` IS that ROM for this purpose.

TWO BUILDS, AND THE SECOND IS WHAT MAKES THE FIRST MEAN ANYTHING:

  DLJUNK=0x61  a unit that cannot be a floppy. The check clamps it to 0 and the
               machine must reach a DESKTOP.
  DLJUNK=1     a LEGAL floppy unit, which the check must leave alone. Drive 1
               is empty on this machine, so the boot must FAIL and say so.

Without the second, a check that ignored DL outright - or a knob that never
reached the register - would pass the first for entirely the wrong reason, and
look identical to the fix working.

It leaves a KNOB KERNEL in build/ while it runs and rebuilds the plain one on
the way out, tests/vgadirty.py's shape and for its reason. That is also why it
is a soak row and serial: a row running beside it would be testing a kernel
somebody else asked for.

GLaBIOS, so a container with no IBM ROM in tools/martypc/roms/ can run it -
tests/bootsmoke.py's reason exactly. The DL check is in the sector and has
nothing to do with the adapter, so one machine is enough.
"""
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88build                                          # noqa: E402
import os88marty                                          # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMG = "build/os8088-360.img"
APPS = "build/apps360.img"


def boots(junk, apps):
    """(reached_a_desktop, the text on screen) for one DLJUNK build.

    EACH VALUE OF DLJUNK GETS ITS OWN TREE (tools/os88build.py). This used to
    be `make DLJUNK=<n>` into build/, which is why the row was `builds=True`:
    it rewrote the shared kernel and every floppy image with it, so nothing
    else could be reading the tree while it ran. The two values are two trees
    now, both cached, and build/ is untouched - so the A/B costs one build per
    value on the first run and nothing on later ones.
    """
    t = os88build.tree("DLJUNK=%s" % junk)
    img = t.img("os8088-360.img")
    try:
        with os88marty.launch(img, apps=apps, machine=MACHINE) as m:
            return True, "\n".join(m.screen())
    except os88marty.MartyError:
        # The desktop gate never fired. Come back with no gate at all and read
        # what IS on the screen, because `Disk error` is the assertion - a
        # machine that hung with a blank screen would be a different defect
        # wearing the same result.
        with os88marty.launch(img, apps=apps, machine=MACHINE, boot=10) as m:
            return False, "\n".join(m.screen())


# --- 0x61: the Packard Bell exactly. Clamped to 0, so it boots. ---------
up, text = boots("0x61", APPS)
check(up, "DLJUNK=0x61 reaches a desktop",
      "SPEC.md 2.9.11's range check should clamp a unit above 3 to 0; a "
      "failure here is the Packard Bell 286 bug, live",
      got=text.strip()[:120] or "(a blank screen)", want="a desktop")

# --- 1: a legal unit, left alone, and drive 1 is empty. ------------------
up, text = boots("1", None)
check(not up, "DLJUNK=1 does NOT reach a desktop",
      "1 is a legal floppy unit and the check must leave it alone. A "
      "desktop here means the sector is clamping DL it should keep - or "
      "that the knob never reached the register, which would make the "
      "first check above pass for no reason at all",
      got="a desktop", want="a boot that fails")
check("Disk error" in text, "...and it says `Disk error`",
      "the sector should reach its own message rather than hanging: a "
      "blank screen here is a different defect with the same verdict",
      got=text.strip()[:120] or "(a blank screen)", want="Disk error")

# NO RESTORE. There used to be a `finally: build()` here putting the plain
# kernel back into build/, because this row had just overwritten it. It builds
# into build/trees/dljunk-<hash>/ now and never touches the shared tree, so
# there is nothing to put back - and a run that dies part way through leaves
# nothing behind either, which the `finally` could only promise and not
# deliver (a killed Python runs no `finally` at all).
done("dljunk")
