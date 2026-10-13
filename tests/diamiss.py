#!/usr/bin/env python3
"""SPEC.md 107.4: a DIA that CANNOT BE FOUND at boot is a graceful failure.

    make && make diatest && python3 tests/diamiss.py [--only ghost|bvol]

Two of `make diatest`'s fixture system disks, one boot each on a VGA XT:

  ghost  a DIA row to SYSTEM\\DIA\\GONE.O88 - which is not on the disk -
         AHEAD of a row to DIAPROBE.O88, which is:
    A  the boot reaches the desktop, and says ONE sentence, the existing
       `Shortcut not found (A:)`;
    B  the missing row is DOWNGRADED to the shortcut it is: its mark is
       gone, and it is PLACED in a desktop cell, captioned `Gone`, where the
       user can double-click it to retry or Delete it - in memory only, so
       SYSTEM.CFG is the fixture still;
    C  the row behind it was still launched: the probe's window is up.
  bvol   a DIA row on B:, and the machine booted with no disk in B::
    A  the desktop, and `Shortcut not found (B:)` - the drive it points at;
    B  the row downgraded and placed, as above; and nothing launched.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): take sc_open's `stc`
after the toast out and B fails on both (the row keeps its mark and no
cell); make dia_boot `jmp kretc_es` at the first miss and ghost's C fails.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88flush   # noqa: E402
import os88dia     # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
DESK_SC0 = os88dia.DESK_SC0
DSL_PIN, DSL_GONE, DSL_SLOT = os88dia.DSL_PIN, os88dia.DSL_GONE, \
    os88dia.DSL_SLOT

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def volume(m, tmp):
    out = os.path.join(tmp, "flushed.img")
    m.pause()
    try:
        m.flush(0, out)
    finally:
        m.run()
    return os88flush.Volume(open(out, "rb").read())


def row(ui, k):
    seg = ui._word("sc_seg")
    return bytes(ui.m.read((seg << 4) + k * os88dia.SC_REC, os88dia.SC_REC))


def downgraded(ui, zone, cap):
    """The row in `zone` lost its mark and holds a cell, PLACED."""
    r = os88dia.parse_row(row(ui, zone - DESK_SC0))
    zb = ui.m.read(ui._S("desk_zslot") + zone, 1)[0]
    check(not r["dia"] and r["kind"] == 1,
          "the row lost its mark - a plain package shortcut now (+3 = %#x)"
          % r["mark"])
    check(zb < DSL_GONE and zb & DSL_PIN and (zb & DSL_SLOT)
          < ui.m.read(ui._S("desk_ncell"), 1)[0],
          "...PLACED in a desktop cell (%#x)" % zb)
    check(r["cap"] == cap, "...captioned %r (%r)" % (cap, r["cap"]))


def leg(name, tmp):
    sysimg = os.path.join(tmp, "dia%s360.img" % name)
    shutil.copy(os.path.join(BDIR, "dia%s360.img" % name), sysimg)
    fixture = open(os.path.join(BDIR, "diafix", name, "system.cfg"),
                   "rb").read()
    print("%s:" % name)
    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        drive = "A" if name == "ghost" else "B"
        print(" A: the desktop, and one sentence")
        txt = ui.wait_toast(says="not found", limit=90)
        check(txt == "Shortcut not found (%s:)" % drive,
              "the toast names the drive (%r)" % txt)
        print(" B: the missing row is a shortcut now")
        downgraded(ui, DESK_SC0, "Gone" if name == "ghost" else "DiaProbe")
        if name == "ghost":
            print(" C: the row behind it still launched")
            ui.wait_window("DiaProbe", limit=60)
            check(True, "the probe's window is up")
            r = os88dia.parse_row(row(ui, 1))
            check(r["dia"] and ui.m.read(ui._S("desk_zslot") + DESK_SC0 + 1,
                                         1)[0] == 0xFF,
                  "...and ITS row is still the DIA row, with no cell")
        else:
            check("DiaProbe" not in ui.titles(),
                  "nothing launched (%r)" % ui.titles())
        check(volume(ui.m, tmp).read("SYSTEM.CFG") == fixture,
              "SYSTEM.CFG is the fixture: the downgrade is in memory")


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    tmp = tempfile.mkdtemp(prefix="diamiss-")
    for name in ("ghost", "bvol"):
        if only in (None, name):
            leg(name, tmp)
    shutil.rmtree(tmp, ignore_errors=True)
    print("diamiss: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
