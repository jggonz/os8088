#!/usr/bin/env python3
"""OSAPI_PKG_START runs an image out of memory, and refuses two (SPEC.md 21.5).

    make && make pkgrun && python3 tests/pkgrun.py

The slot exists for the Wire (SPEC.md 26.7), which fetches a `.O88` over the
network into a claim and has no file to name. This is the gate on it, and it
is the `mseg`/`covl` shape: a TEST package that no shipped floppy carries,
built by `make pkgrun`, booted in B: and asked three questions.

**MARTYPC, THROUGH `os88ui`, AND IT USED TO BE HAND-ROLLED QEMU.** This row's
own header argued the other way - *"nothing here is a time, and the three
answers are all state; MartyPC would do as well and costs ten times the wall
clock"* - and that is exactly the argument docs/TESTING.md refuses: `pkgrun`
is on none of the seven entries of the QEMU list, and "it is quicker" is not
one of them. It also did not survive its own evidence. What it cost instead
was a row that FLAKES, and in the worst way a row can:

  * it drove the desktop with REMEMBERED COORDINATES - `dbl(600, 110)` for the
    B: zone, `time.sleep(8)`, then `dbl(160, 144)` for "the second row" - so
    every wait was a host sleep on a guest whose rate moves with the box's
    load (docs/plans/SOAK-PARALLEL.md 1), and a miss raised nothing;
  * and it read `inst_tab` - 384 bytes - off a RUNNING machine in its poll
    loop. A record is 32 bytes the kernel writes field by field, so a read
    landing mid-write returns a live record with a torn name. That is what a
    soak caught: `instances: Disk@D1E4, KBR@N@9F00, ...`, two names of
    garbage, reported as `no live instance named PKGRUN: the gate's own
    package did not launch` - while the SCREENSHOT saved beside it showed
    PKGRUN's window with all three answers `ok` and HELLO's window open
    behind it. The machine was right and the reading was wrong, which is the
    single most expensive shape a test failure can have.

So the navigation is `ui.path("B:/PKGRUN.O88")` - every step confirmed against
the guest's own tables, no coordinate anywhere - and every read of the
instance table goes through `os88marty.quiesce`, which is `settle`'s signal
applied to those bytes: identical readings a GUEST interval apart, so a torn
record cannot be the one that is believed.

WHAT IT ASSERTS, and the first one is asserted against the KERNEL:

  A  an instance named HELLO is LIVE in `inst_tab` - the kernel's own table,
     read through the symbol map, so the pass does not rest on the test
     package's opinion of what happened - AND the region that instance names
     is byte-for-byte `build/hello.o88`. The second half is not belt and
     braces: the slot's first version lost the source OFFSET (it read it out
     of the caller's segment instead of the kernel's) and copied from
     whatever was next to the caller's claim, which still passed
     `ld_check_hdr`, still registered an instance, and then far-called a
     dispatcher that was not one. `hello.o88` is the shipped one and not a
     fixture: the claim is that the slot runs an ORDINARY package.
  B  a spoiled magic answers CF=1 with AL = LD_EBAD.
  C  header flags bit 2 - a package carrying PARTS (SPEC.md 20.12), which are
     read out of a FILE that does not exist here - answers CF=1 / LD_EBAD too,
     and by a different route: the flags test is made before ld_check_hdr,
     which allows the bit.

AND THREE ON THE OTHER FORM OF THE SAME CELL (SPEC.md 21.5), which is why this
row is worth more than it was. `OSAPI_PKG_START` takes a NAME and, optionally,
an image you are holding - ONE door, two forms - and what the pair settles is
that C's refusal belongs to NOT HAVING A FILE and not to the slot:

  D  HELLO.O88 runs BY NAME too, so `inst_tab` ends with TWO live records
     named HELLO, one per door.
  E  **ONE FILE, ONE CELL, TWO ANSWERS.** MSEG.O88 - tests/multiseg's real
     seven-part package, not a flag set by hand - is read into a claim and
     handed to the IMAGE form, which must REFUSE it; then the same name is
     given to the BY-NAME form, which must RUN it. Both halves are asserted,
     because either alone is a claim about one form rather than about the
     difference.
     And the parts REALLY ARRIVE: MSEG rewrites its own window title to
     `MSEG 5/5 OK` (tests/multiseg.py reads the same word), so a launch that
     produced a window and no parts cannot pass this.
  F  a name that is not there answers CF=1 / LD_EBAD - which by name is the
     same code as `that file is not a package`, because dskw_stat fails first
     and the two arrive as one (SPEC.md 21.4).

B and C also say something A cannot: the region and the instance record a
refused load reserved were given back. Three loads happen in this session and
the heap is small; a leak of either shows up as C failing with LD_ENOMEM.

HOW THE VERDICT IS READ. The test package writes a 14-byte block at offset 32
of its own image - immediately after the header, before any code - opening
with the tag 'PR'. The host finds the live instance named PKGRUN in
`inst_tab`, takes its `I_SPTR` (the region's base segment, SPEC.md 20.1) and
reads the block there. No map of the test package is needed and none can go
stale; the tag is the check that the pointer was followed correctly.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88parts                                             # noqa: E402
import os88ui                                                # noqa: E402

# THE GLaBIOS TWIN BY NAME, which is what t_machines requires: the period
# 5150 ROM is not in this tree (CONTRIBUTING.md 6), so naming it runs on
# the twin anyway on a box without a private copy - and says nothing.
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"

# kernel/instance.inc, mirrored - the record layout is ABI (SPEC.md 20.9)
I_STATE, I_SPTR, I_NAME, I_RECSZ, INST_MAX = 0, 6, 12, 32, 12

PR_OFF = 32                     # the verdict block, at the head of the image
PR_LEN = 25
LD_EBAD = 2                     # SPEC.md 21.4
# HOW MANY PARTS MSEG HAS IS MSEG'S OWN ANSWER, read out of the .o88's part
# table (tools/os88parts.py), which is what tests/multiseg.py does and for the
# reason written there: it was 6, then 7, and a number copied into a second
# file is a number that goes stale. It is read LAZILY because the fixture is
# `make pkgrun`'s to build and this module is imported before that runs.


fails = []


def say(msg):
    print("  " + msg)
    sys.stdout.flush()


def build():
    """The kernel under test and the gate's own disk, for a reader running
    this script BY HAND - who should not have to know the target's name.

    **NOT INSIDE A SOAK.** The row declares `wants=` now rather than
    `builds=True` (docs/WRITING-TESTS.md), so under a frozen tree the disk is
    already there, built with every other row's before anything started - and
    a `make` here would write the SHARED build/ in the middle of a run, which
    is the four-minute window that cost nine rows once
    (docs/plans/SOAK-PARALLEL.md 12). It used to be builds=True with no
    pkgrun360.img in the `wants=` union, so the frozen tree did not carry the
    disk at all and the row died on FileNotFoundError in a third of a second,
    which reads as a broken loader rather than a missing file."""
    if os88build.tree_root():
        return
    subprocess.run(["make", "-s", "build/os8088-360.img", "pkgrun"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)


def _table(ui):
    """The whole instance table, as bytes. ONE read, so every record in a
    snapshot came from one moment."""
    return ui.m.read(ui.sym("inst_tab"), I_RECSZ * INST_MAX)


def _decode(b):
    """[(name, sptr)] of every LIVE record in a snapshot."""
    out = []
    for i in range(INST_MAX):
        r = b[i * I_RECSZ:(i + 1) * I_RECSZ]
        if r[I_STATE] != 1:
            continue
        name = bytes(r[I_NAME:I_NAME + 16]).split(b"\0")[0].decode(
            "ascii", "replace")
        out.append((name, r[I_SPTR] | (r[I_SPTR + 1] << 8)))
    return out


def instances(ui, settled=True):
    """[(name, sptr)] of every LIVE instance record.

    `settled` reads it through `quiesce` - the same bytes twice a guest
    interval apart - because a 32-byte record is written field by field and a
    read that lands mid-write returns a live record with a torn name. That is
    not hypothetical; it is what this row used to fail as."""
    if settled:
        os88marty.quiesce(ui.m, lambda: _table(ui),
                          what="the instance table to stop changing")
    return _decode(_table(ui))


def slots(ui):
    """Every record, live or not, with its name - the diagnostic behind the
    entry-count note: two PKGRUN instances would show here even if the second
    had gone."""
    b = _table(ui)
    out = []
    for i in range(INST_MAX):
        r = b[i * I_RECSZ:(i + 1) * I_RECSZ]
        nm = bytes(r[I_NAME:I_NAME + 16]).split(b"\0")[0].decode(
            "ascii", "replace")
        if r[I_STATE] or nm:
            out.append("%d:%s/%d@%04X" % (i, nm or "-", r[I_STATE],
                                          r[I_SPTR] | (r[I_SPTR + 1] << 8)))
    return " ".join(out)


def seg_of(ui, want, settled=True):
    return dict(instances(ui, settled)).get(want, 0)


def mseg_parts():
    """MSEG.O88's own part count, out of its part table."""
    blob = open(os88build.at("build/mseg.o88"), "rb").read()
    return len(os88parts.rows(blob[:blob[8] | (blob[9] << 8)]))


def mseg_title(ui):
    """MSEG's own window title, which is its verdict on its five parts.

    Read off the WINDOW and not the instance record, because the record's name
    is the package's and the title is what MSEG rewrites (tests/multiseg.py
    does the same walk)."""
    import struct
    import os88geom
    m = ui.m
    for slot in range(8):
        wp = os88geom.winptr(m, slot, m.sym)
        seg = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
        toff = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
        if not seg or not toff:
            continue
        t = m.read((seg << 4) + toff, 24).split(b"\0")[0].decode(
            "ascii", "replace")
        if t.startswith("MSEG"):
            return t
    return ""


def main():
    os.chdir(ROOT)
    build()

    # THE 360KB PAIR, because the machine is a 5150 and a 5150 has 360KB
    # drives. `make pkgrun` builds both geometries; the QEMU version took the
    # 1.44MB one because QEMU's floppy will take any image, and handing it to
    # a period machine is a boot that never reaches a desktop.
    with os88ui.boot("build/os8088-360.img", apps="build/pkgrun360.img",
                     machine=MACHINE) as ui:
        # --- open B: and launch the gate's own package ----------------------
        # By NAME. The drive, the row and the launch are each confirmed against
        # the guest's own tables, so a miss raises HERE naming what it saw
        # rather than twenty steps later as a missing instance.
        ui.path("B:/PKGRUN.O88")

        # --- wait for the package to finish its three checks -----------------
        # On [pr_done] in its own image, on the GUEST's clock. The package
        # guards itself on its entry count, so what this waits for is the
        # checks being FINISHED and not merely the wake having arrived.
        def done():
            seg = seg_of(ui, "PKGRUN", settled=False)
            return bool(seg) and bool(ui.m.read(seg * 16 + PR_OFF, 3)[2])

        os88marty.until(ui.m, lambda _: done(),
                        "PKGRUN to finish its six checks", limit=180.0)

        # --- what the KERNEL says -------------------------------------------
        live = instances(ui)
        names = [n for n, _ in live]
        say("instances: " + (", ".join("%s@%04X" % (n, g) for n, g in live)
                             or "(none)"))
        say("slots: " + slots(ui))
        seg = dict(live).get("PKGRUN", 0)
        if not seg:
            fails.append("no live instance named PKGRUN: the gate's own "
                         "package did not launch, so nothing below ran")
        if names.count("HELLO") < 2:
            fails.append("A/D: %d live instance(s) named HELLO, want 2 - one "
                         "per door. OSAPI_PKG_START ran the image and "
                         "OSAPI_PKG_START ran the same file by name, and they "
                         "are separate instances (SPEC.md 21.5)"
                         % names.count("HELLO"))
        if "MSEG" not in names:
            fails.append("E: no live instance named MSEG - the file "
                         "the image form refused was not run by name either, "
                         "so the pair says nothing (SPEC.md 21.5)")
        else:
            # ...AND ITS PARTS ARRIVED. MSEG rewrites its own window title to
            # `MSEG n/n OK` once it has checked every part three ways
            # (tests/multiseg.py reads the same word), so this is the
            # difference between `a window appeared` and `the launch worked`.
            title = mseg_title(ui)
            n = mseg_parts()
            want = "MSEG %d/%d OK" % (n, n)
            say("MSEG's own verdict: %r" % title)
            if title != want:
                fails.append("E: MSEG launched by name and its title reads "
                             "%r, not %r - it ran, and its PARTS did not "
                             "arrive, which is the whole of what this door "
                             "exists to do (SPEC.md 21.5, 20.12)"
                             % (title, want))
        if "HELLO" in names:
            # --- AND THE COPY LANDED, byte for byte -------------------------
            # An instance existing says the slot returned; it does not say it
            # copied the RIGHT bytes. The first version of the slot read the
            # source OFFSET out of the caller's segment instead of the
            # kernel's and copied from whatever was there - which passed
            # ld_check_hdr (that reads the caller's bytes, before the copy),
            # registered an instance, and then far-called a dispatcher that
            # was not one. So the region is compared against the FILE.
            hseg = dict(live)["HELLO"]
            want = open(os88build.at("build/hello.o88"), "rb").read()
            got = bytes(ui.m.read(hseg * 16, min(len(want), 512)))
            if got != want[:len(got)]:
                n = next((i for i in range(len(got))
                          if got[i] != want[i]), 0)
                fails.append("A: the copy is wrong at byte %d - the region "
                             "holds %02X where build/hello.o88 has %02X. The "
                             "instance exists, so the slot RETURNED; what it "
                             "copied is not the image it was given "
                             "(SPEC.md 21.5)" % (n, got[n], want[n]))

        # --- ...and what the package recorded --------------------------------
        if seg:
            b = ui.m.read(seg * 16 + PR_OFF, PR_LEN)
            say("verdict raw: " + bytes(b).hex())
            if bytes(b[:2]) != b"PR":
                fails.append("the verdict block at PKGRUN:%04X is %r, not "
                             "'PR' - I_SPTR did not name the image"
                             % (PR_OFF, bytes(b[:2])))
            else:
                done_n, ok = b[2], b[3]
                cfa, cfb, cfc = b[4], b[5], b[6]
                ala, alb, alc = b[7], b[8], b[9]
                ferr, ln, ent = b[10], b[11] | (b[12] << 8), b[13]
                cfd, cfe, cff = b[14], b[15], b[16]
                ald, ale, alf = b[17], b[18], b[19]
                cfe1, ale1 = b[20], b[21]
                ferr2, ln2 = b[22], b[23] | (b[24] << 8)
                say("pkgrun: done %d ok %02X  A cf%d al%d  B cf%d al%d  "
                    "C cf%d al%d  ferr %d len %d entries %d"
                    % (done_n, ok, cfa, ala, cfb, alb, cfc, alc, ferr, ln,
                       ent))
                say("pkgopen: D cf%d al%d  E run cf%d al%d / open cf%d al%d  "
                    "F cf%d al%d  ferr %d len %d"
                    % (cfd, ald, cfe1, ale1, cfe, ale, cff, alf, ferr2, ln2))
                if ent != 1:
                    # REPORTED, NOT FAILED. More than one wake per post is the
                    # kernel putting one back that a drag or a launch ate
                    # (SPEC.md 74.1.1); the package's entry-count guard is what
                    # makes it harmless, and `done` says which entry finished.
                    say("note: the wake handler was entered %d times and the "
                        "checks finished on entry %d - the guard held"
                        % (ent, done_n))
                    say("note: every instance slot: " + slots(ui))
                if not done_n:
                    fails.append("the wake handler never ran to the end - the "
                                 "checks did not happen")
                elif ferr and not ok:
                    fails.append("OSAPI_FILE_READ of HELLO.O88 answered "
                                 "FERR %d, so no check ran" % ferr)
                else:
                    if not ln:
                        fails.append("HELLO.O88 read back as 0 bytes")
                    if (cfa, ala) != (0, 0):
                        fails.append("A: the slot answered CF=%d AL=%d, want "
                                     "CF=0 AL=0" % (cfa, ala))
                    if (cfb, alb) != (1, LD_EBAD):
                        fails.append("B: a spoiled magic answered CF=%d AL=%d, "
                                     "want CF=1 AL=%d (LD_EBAD)"
                                     % (cfb, alb, LD_EBAD))
                    if (cfc, alc) != (1, LD_EBAD):
                        fails.append("C: header flags bit 2 answered CF=%d "
                                     "AL=%d, want CF=1 AL=%d (LD_EBAD) - a "
                                     "package with PARTS reads them from its "
                                     "own FILE (SPEC.md 20.12)"
                                     % (cfc, alc, LD_EBAD))
                    # --- the other door (SPEC.md 21.5) ---------------------
                    if ferr2:
                        fails.append("E: OSAPI_FILE_READ of MSEG.O88 answered "
                                     "FERR %d, so the half of E that hands "
                                     "the file to the image form never ran" % ferr2)
                    elif not ln2:
                        fails.append("E: MSEG.O88 read back as 0 bytes")
                    if (cfd, ald) != (0, 0):
                        fails.append("D: OSAPI_PKG_START answered CF=%d AL=%d "
                                     "for HELLO.O88, want CF=0 AL=0. It is "
                                     "ld_run_name with no poster - the same "
                                     "pipeline a double-click takes "
                                     "(SPEC.md 21.5)" % (cfd, ald))
                    if (cfe1, ale1) != (1, LD_EBAD):
                        fails.append("E: the IMAGE form answered CF=%d AL=%d for the "
                                     "REAL parted MSEG.O88, want CF=1 AL=%d. "
                                     "Without this half, E's other half is a "
                                     "claim about one form and not about the "
                                     "difference" % (cfe1, ale1, LD_EBAD))
                    if (cfe, ale) != (0, 0):
                        fails.append("E: the BY-NAME form answered CF=%d AL=%d for "
                                     "MSEG.O88, want CF=0 AL=0. The kernel "
                                     "reads the FILE here, so a package "
                                     "carrying parts has one to read them "
                                     "out of (SPEC.md 21.5)" % (cfe, ale))
                    if (cff, alf) != (1, LD_EBAD):
                        fails.append("F: a name that is not there answered "
                                     "CF=%d AL=%d, want CF=1 AL=%d"
                                     % (cff, alf, LD_EBAD))

        if fails:
            shot = os.path.join(ROOT, "build", "pkgrun.png")
            try:
                w, h, data = ui.m.fbuf()
                os88marty.write_png_rgb(shot, w, h, data)
                say("screen: " + shot)
            except Exception as e:                            # noqa: BLE001
                say("screen: could not be taken (%s)" % e)

    for f in fails:
        print("FAIL " + f)
    print("pkgrun: %s" % ("OK - the image form ran one and refused two; the by-name form ran two more, one of them the very file "
                          "the back half refused"
                          if not fails
                          else "%d assertion(s) failed" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
