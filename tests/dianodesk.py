#!/usr/bin/env python3
"""SPEC.md 11.103.10: with NO DESK.DRV on the system disk, a DIA row still
comes up at boot - as a TITLED widget - and its row is kept.

    make && python3 tests/dianodesk.py [--only absent|present]

diamiss's shape, one boot per leg on MartyPC's VGA XT. Each leg is the
SHIPPED 360KB system disk copied to a scratch file, with a SYSTEM.CFG that
tools/os88dia.py writes - one DIA row, A:SYSTEM\\DIA\\DESKCAL.O88 - put on
it by mtools; leg `absent` also DELETES DESK.DRV from the copy (mdel), and
asserts on the host that the file is gone from it and was on the original,
so a fixture that never had the module cannot pass for one that lost it.

  absent  DESK.DRV deleted:
    A  the boot reaches the desktop and the Desk Calendar is UP - dia_boot is
       resident (SPEC.md 110.4), so the relaunch does not need the module;
    B  it is TITLED: no WF_DESK, a title bar (not WF_FULL) - its QUERY was
       refused (mod_need found no file) and it took kern_small's path;
    C  the module is NOT held: mod_tab's MOD_DESK row is 0 and every DKDFP
       slot still names mod_gone;
    D  its row is KEPT: still the DIA row (+3 = 'D'), with no desktop cell -
       a refused QUERY is not a missing target, and nothing downgrades it.
  present the same disk with DESK.DRV left on it - the control:
    A  the calendar is up, B a DESK window (WF_DESK, no title bar), C the
       module HELD (the row's segment non-zero, the hover slot not mod_gone),
       D the row kept the same way.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): move dia_boot's loop into
DESK.DRV (the cut design's first draft) and `absent` A fails - no window at
all; make osapi_wm_desk_x's QUERY answer CF = 0 without mod_need and
`absent` B fails (MAKE then refuses into a desk-sized titled frame, and
`present` C fails: nothing loads the image).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88dia     # noqa: E402
import os88flush   # noqa: E402
import os88geom as geom  # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
DESK_SC0 = os88dia.DESK_SC0
TITLE = "Desk Calendar"
MOD_DESK = 7                    # kernel/mod.inc (SPEC.md 11.103.10)
MODR_SIZE = 4
DKD_NENT = 4
MTENV = dict(os.environ, MTOOLS_SKIP_CHECK="1")

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def has_file(img, name):
    """Is `name` in the volume's ROOT? (an independent FAT12 reader)"""
    v = os88flush.Volume(open(img, "rb").read())
    try:
        v.read(name)
        return True
    except Exception:
        return False


def fixture(tmp, leg):
    img = os.path.join(tmp, "nodesk-%s.img" % leg)
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88dia.py"),
                    "cfg", "-o", cfg, "--dia",
                    "A:SYSTEM/DIA/DESKCAL.O88=Calendar"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["mcopy", "-o", "-i", img, cfg, "::/SYSTEM.CFG"],
                   check=True, stdout=subprocess.DEVNULL, env=MTENV)
    check(has_file(img, "DESK.DRV"),
          "the shipped 360KB system disk carries DESK.DRV")
    if leg == "absent":
        subprocess.run(["mdel", "-i", img, "::/DESK.DRV"], check=True,
                       stdout=subprocess.DEVNULL, env=MTENV)
        check(not has_file(img, "DESK.DRV"),
              "...and the scratch copy does not any more")
    return img


def leg(name, tmp):
    img = fixture(tmp, name)
    absent = name == "absent"
    print("%s:" % name)
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        m = ui.m
        print(" A: the boot relaunched the calendar")
        w = ui.wait_window(TITLE, limit=120)
        check(w is not None, "the Desk Calendar is up")
        f = w.flags
        print(" B: %s" % ("TITLED - its QUERY was refused" if absent
                          else "a DESK window"))
        desk = bool(f & geom.WF_DESK)
        full = bool(f & geom.WF_FULL)
        if absent:
            check(not desk and not full,
                  "no WF_DESK and a title bar (W_FLAGS %#06x)" % f)
        else:
            check(desk, "WF_DESK, the widget (W_FLAGS %#06x)" % f)
        print(" C: the module %s" % ("is not held" if absent else "is held"))
        seg = struct.unpack("<H", bytes(m.read(
            ui._S("mod_tab") + MOD_DESK * MODR_SIZE, 2)))[0]
        gone = ui._S("mod_gone")
        slots = []
        raw = bytes(m.read(ui._S("DKDFP"), DKD_NENT * 4))
        for k in range(DKD_NENT):
            slots.append(struct.unpack_from("<HH", raw, k * 4))
        flat = ["%05X" % ((sg << 4) + off) for off, sg in slots]
        if absent:
            check(seg == 0, "mod_tab's MOD_DESK row is 0 (%#06x)" % seg)
            check(all((sg << 4) + off == gone for off, sg in slots),
                  "every DKDFP slot names mod_gone (%r)" % flat)
        else:
            check(seg != 0, "mod_tab's MOD_DESK row holds a claim (%#06x)"
                  % seg)
            check(all(sg == seg for off, sg in slots),
                  "every DKDFP slot points into it (%r)" % flat)
        print(" D: the row is kept")
        sseg = ui._word("sc_seg")
        r = os88dia.parse_row(bytes(m.read(sseg << 4, os88dia.SC_REC)))
        zb = m.read(ui._S("desk_zslot") + DESK_SC0, 1)[0]
        check(r["dia"] and r["kind"] == 1 and zb == 0xFF,
              "still the DIA row (+3 = %#x), with no cell (%#x)"
              % (r["mark"], zb))


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    tmp = tempfile.mkdtemp(prefix="dianodesk-")
    try:
        for name in ("absent", "present"):
            if only in (None, name):
                leg(name, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("dianodesk: %s" % ("PASS" if not fails else
                             "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
