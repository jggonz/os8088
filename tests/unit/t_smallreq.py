#!/usr/bin/env python3
"""Nothing on a small floppy may need something `kern_small` has not got.

    python3 tests/unit/t_smallreq.py

SPEC.md 24.5 is one sentence - *if it can never run there, it does not go on
the disk* - and SPEC.md 24.5.3/24.5.4 are the same sentence pointed at a
document with no program, a program with no document, and a file for another
computer whose driver on this one is absent. Every one of those is decided in
the Makefile by a `filter-out` over a list, and **a `filter-out` that matches
nothing is silent**: the build stays green, `os88disk.py` is handed a name it
can read, and the floppy ships.

That is not hypothetical, it is the reason this file exists. Five separate
things reached `make small` / `make smallapps` and none of them made a build
step go red:

  * `BROWSER.HTM` and `BEVERLY.MOD`, because `$(SMALLOMIT_DATA)` spelled the
    two files as `apps/browser/browser.htm` and `apps/tracker/beverly.mod`
    while the `$(PKGZ)` arm - the arm that ships - had redefined
    `$(APPS_DATA)` to packed copies under `$(ZDATA)/`. The filter matched
    nothing from the day it was written.
  * `SHEET.O88`, whose ground SPEC.md 24.5.2 published and whose name nobody
    put in `$(SMALLOMIT)`.
  * `CHART.O88`, which followed Sheet onto the disk and has no other launch
    path than a file Sheet writes.
  * `FONTVIEW.O88`, which arrived by inheritance the day PR #158 put it in
    `$(CORE_TOOLS)` - onto two floppies that carry no `SYSTEM/FONTS/` at all.
    SPEC.md 90.3 later reasoned about exactly this and vouched for it, over
    two lists neither of which is the folder the package needs.
  * `SYSTEM/DOS/OS88NET.COM`, the DOS end of a parallel link whose os8088
    end (`NET.DRV`) is in no small driver set.

So this reads the BUILT IMAGE and not the variable, which is the same
argument `tests/unit/t_appsmall.py`'s own disk walk makes: the Makefile is
what is being checked, so asking the Makefile is asking the defendant.

**The two halves are shaped differently on purpose.** Drivers are an
ALLOWLIST - `kern_small` ships exactly five `.DRV` files and they are all its
own on-demand kernel modules - because a driver added to `$(DRIVERS)` two
years from now must fail here without anyone remembering to add a row.
Packages and data files are a DENYLIST with a reason per row, because there
is no allowlist to write: which programs belong on the small floppies is a
decision that moves, and a list of names would have to be edited by whoever
adds a package rather than by whoever breaks the rule.

**Break it on purpose to see it work** (docs/WRITING-TESTS.md 1): put
`$(BUILD)/sheet.o88` back in `$(APPS_TOOLS)`'s reach by deleting it from
`$(SMALLOMIT)`, `make smallapps`, and this goes red naming the package, the
image and the requirement.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                             # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ROOT = os.path.abspath(ROOT)

# THE SYSTEM PAIR AND THE APPS PAIR, both geometries of each. All four are
# checked against the same rules because both halves of a pair go in the same
# machine (`make small` boots and `make smallapps` goes in B:), and the one
# defect the pairing has already produced - the 1.44MB half built differently
# from the 360KB one - is exactly what a per-image list would hide again.
IMGS = ["build/small360.img", "build/small.img",
        "build/smallapps360.img", "build/smallapps.img"]

# The ONLY .DRV files legal on a small floppy: `$(SMALLDRIVERS)` is `$(KMODS)`
# plus `$(SMALLMODS)` and NOT ONE ORDINARY DRIVER. These five are not drivers
# at all in the sense the Drivers page means - they are this kernel's own code
# cut out of its own binary by tools/os88mod.py (SPEC.md 2.8), and they are
# `.DRV` only because os88disk.py stamps that extension read-only+hidden+system
# and `ld_check_hdr` refuses to launch one.
#
# AN ALLOWLIST AND NOT A LIST OF THE FORBIDDEN, which is the point: SOUND.DRV,
# HDD.DRV, NET.DRV, ETHER.DRV, RAMDISK.DRV, RAMPAGE.DRV, HDDTOOL.DRV,
# XMEM.DRV, SAVER.DRV and VMMOUSE.DRV are all out today, and so is the
# eleventh one nobody has written.
DRV_OK = {"CTRL.DRV", "FORMAT.DRV", "CLONE.DRV", "FILECP.DRV", "FDLG.DRV"}

# (8.3 name, why it may not be on a small floppy). One row per SPEC.md 24.5
# ground; the § is in the reason so a failure sends the reader to the argument
# and not to this file.
FORBIDDEN = {
    # 24.5 - the driver is not in $(SMALLDRIVERS)
    "BROWSER.O88":  "ETHER.DRV is in no small driver set (SPEC.md 24.5, 72)",
    "FTPD.O88":     "ETHER.DRV is in no small driver set (SPEC.md 24.5, 72)",
    "TELNET.O88":   "ETHER.DRV is in no small driver set (SPEC.md 24.5, 72)",
    "THEWIRE.O88":  "ETHER.DRV is in no small driver set (SPEC.md 24.5, 92)",
    "MODPLUG.O88":  "SOUND.DRV is in no small driver set (SPEC.md 24.5, 34)",
    "TRACKER.O88":  "SOUND.DRV is in no small driver set (SPEC.md 24.5, 34)",
    "AUDIO.O88":    "SOUND.DRV is in no small driver set (SPEC.md 24.5, 34)",
    # 24.5 - a claim the floor machine cannot fund, made where it cannot refuse
    "SKIES.O88":    "a 32KB claim inside the fsx bracket - the refusal is a "
                    "black screen (SPEC.md 24.5, 88)",
    # DOTDEL.O88 IS NOT A ROW. It was omitted beside SKIES on a ground that
    # SPEC.md 5.4.2.5.1 withdrew (kern_small has a `gfx_blit1` body now), and
    # SPEC.md 24.5.5 is the measurement that put it back on the floppy. A name
    # left here would be a filter that reads like a decision and is a no-op,
    # which is the shape SPEC.md 24.5 already names for RECORDER.
    # 24.5.2 - a minimum claim larger than the machine's whole RAM
    "SHEET.O88":    "claims ~100KB on open, more RAM than a 128KB machine has "
                    "in total (SPEC.md 24.5.2)",
    # 24.5.3 - a reader with nothing on the disk to read
    "CHART.O88":    "declares no association, so its only launch path is File "
                    "> Open on a file only SHEET writes (SPEC.md 24.5.3)",
    "FONTVIEW.O88": "the small system disks carry no SYSTEM/FONTS/ at all - "
                    "ty_gofonts walks to exactly that folder (SPEC.md 24.5.3)",
    # 24.5.4 - a program for the far machine whose near end is absent
    "OS88NET.COM":  "the DOS end of a parallel link whose os8088 end, NET.DRV, "
                    "is in no small driver set (SPEC.md 24.5.4, 62)",
    # 24.5 - the data files, whose readers are all above
    "BROWSER.HTM":  "the Browser's own manual, and nothing else on the machine "
                    "opens a .HTM (SPEC.md 24.5, 71.12)",
    "BEVERLY.MOD":  "the module for two players that are not on this disk "
                    "(SPEC.md 24.5, 24.4)",
}

# ...and the rule the denylist above is a snapshot of, checked SEPARATELY so
# that the NEXT data file is covered without being named. A document whose
# only readers are packages is bytes nothing can open unless one of them is on
# the same volume - SPEC.md 24.5's "one step along", stated as a rule rather
# than as two names.
#
# It is keyed on the EXTENSION, because that is what the machine dispatches on
# (SPEC.md 54) and what a new document would arrive carrying. An extension no
# row names is not checked: this says "if you ship a .HTM, ship something that
# reads one", never "these are the only documents allowed".
READERS = {
    "HTM": ("BROWSER.O88",),
    "MOD": ("MODPLUG.O88", "TRACKER.O88"),
    "SLK": ("SHEET.O88", "CHART.O88"),
    "DIF": ("SHEET.O88", "CHART.O88"),
    "F88": ("FONTVIEW.O88",),
    "TEX": ("TEXPAD.O88",),
    "MD":  ("ARTFUL.O88",),
}


def names(vol):
    """(folder, 8.3 NAME) for every file on the volume, directories excluded."""
    out = []
    for folder, name11, attr, _clus, _size in vol.walk():
        if attr & 0x10:                                     # a directory
            continue
        stem = name11[:8].decode("ascii", "replace").strip()
        ext = name11[8:].decode("ascii", "replace").strip()
        out.append((folder, (stem + "." + ext if ext else stem).upper()))
    return out


def main():
    try:
        from t_image import Vol
    except Exception as e:                                  # pragma: no cover
        check(False, "the FAT12 reader loads", got=str(e), want="t_image.Vol")
        done("t_smallreq")
        return

    seen = 0
    pkgs = {}
    for rel in IMGS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            # NOT a silent pass. `make` does not build these - `make small`
            # and `make smallapps` do - so the row declares them in `wants=`
            # and the runner builds them before anything runs. Reaching here
            # means that did not happen, and saying so is the difference
            # between this row and the false green docs/plans/SOAK-PARALLEL.md 6
            # records.
            check(False, "%s is present to walk" % rel,
                  "declared in this row's wants=, so the runner builds it "
                  "before any row starts; a missing image means the row "
                  "answered nothing",
                  got="missing", want="run `make small smallapps`")
            continue
        seen += 1
        with open(path, "rb") as f:
            vol = Vol(f.read(), rel)
        here = names(vol)
        present = {n for _f, n in here}
        pkgs[rel] = {n for _f, n in here if n.endswith(".O88")}

        for folder, nm in here:
            if nm.endswith(".DRV"):
                check(nm in DRV_OK, "%s: %s%s is a driver kern_small has"
                                    % (rel, folder, nm),
                      "$(SMALLDRIVERS) is $(KMODS) plus $(SMALLMODS) and not "
                      "one ordinary driver, so the only .DRV files legal on a "
                      "small floppy are this kernel's own on-demand modules "
                      "(SPEC.md 2.8, 24.5). A driver here is a Drivers-page "
                      "row that loads an image for hardware support this "
                      "kernel was built without",
                      got=nm, want="one of " + ", ".join(sorted(DRV_OK)))
            why = FORBIDDEN.get(nm)
            check(why is None, "%s: %s%s is not on this floppy" % (rel, folder, nm),
                  "SPEC.md 24.5 - if it can never run there, it does not go "
                  "on the disk. The Makefile decides this with a filter-out "
                  "over $(SMALLOMIT), $(SMALLOMIT_ORPHAN) or "
                  "$(SMALLOMIT_DATA), and a filter that matches nothing is "
                  "SILENT: the build stays green and the floppy ships",
                  got=why or "", want="omitted")

            ext = nm.rsplit(".", 1)[-1] if "." in nm else ""
            readers = READERS.get(ext)
            if readers and not (present & set(readers)):
                check(False, "%s: %s%s has a reader on this volume"
                             % (rel, folder, nm),
                      "SPEC.md 24.5 one step along - a document is openable "
                      "by nothing else on the machine, so a copy on a disk "
                      "its program is not on is worse than no file at all. "
                      "This is the RULE behind the two names in "
                      "$(SMALLOMIT_DATA) and it covers the next document "
                      "without being told about it",
                      got="no %s reader on %s" % (ext, rel),
                      want="one of " + ", ".join(readers))

    # --- ...AND THE SYSTEM DISK IS A WHOLE SYSTEM (SPEC.md 24.5.6) --------
    # `make small`'s floppy carries everything `make smallapps` writes, so a
    # 128KB machine with ONE DRIVE has the whole system on the disk it booted
    # from. This is the property, not the cluster count: a package that reaches
    # the apps floppy and not the system one puts that machine back to swapping
    # disks for it, silently, because both disks still build and both still
    # boot.
    #
    # STATED AS A SUBSET AND NOT AS EQUALITY, deliberately. The system disk may
    # carry MORE - `TASKMGR.O88` is a $(SYSAPPS) package and would be there on
    # its own - and the day 24.5.6's 108 spare clusters run out the system disk
    # goes back to a curated subset, which is a decision somebody takes: this
    # row then fails naming the package, which is the conversation happening
    # rather than the regression shipping.
    for sysimg, appsimg in (("build/small360.img", "build/smallapps360.img"),
                            ("build/small.img", "build/smallapps.img")):
        if sysimg not in pkgs or appsimg not in pkgs:
            continue
        missing = sorted(pkgs[appsimg] - pkgs[sysimg])
        check(not missing,
              "%s carries everything %s does" % (sysimg, appsimg),
              "SPEC.md 24.5.6 - the small SYSTEM disk is the whole system, so "
              "a 128KB machine with one drive never swaps floppies. A package "
              "on the apps disk and not this one is that machine quietly "
              "losing it; if the disk is full, that is a decision to take in "
              "the Makefile and here, not a build that still goes green",
              got=", ".join(missing) or "nothing missing",
              want="every package on " + appsimg)

    check(seen == len(IMGS), "all four small floppies were walked",
          "both geometries of both disks, because a pair built two different "
          "ways is the defect this cannot be allowed to hide",
          got="%d of %d" % (seen, len(IMGS)), want=str(len(IMGS)))
    done("t_smallreq")


if __name__ == "__main__":
    main()
