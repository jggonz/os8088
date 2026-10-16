#!/usr/bin/env python3
"""SPEC.md 11.103.10: with NO DESK.DRV on the system disk, a Sticky App row still
comes up at boot - as a TITLED widget - and its row is kept.

    make && python3 tests/stiknodesk.py [--only absent|present|bdrive]

stikmiss's shape, one boot per leg on MartyPC's VGA XT. Each leg is the
SHIPPED 360KB system disk copied to a scratch file, with a SYSTEM.CFG that
tools/os88stik.py writes - one Sticky App row, A:STIKAPPS\\DESKCAL.O88 - put on
it by mtools; leg `absent` also DELETES DESK.DRV from the copy (mdel), and
asserts on the host that the file is gone from it and was on the original,
so a fixture that never had the module cannot pass for one that lost it.

  absent  DESK.DRV deleted:
    A  the boot reaches the desktop and the Desk Calendar is UP - stik_boot is
       resident (SPEC.md 110.4), so the relaunch does not need the module;
    B  it is TITLED: no WF_DESK, a title bar (not WF_FULL) - its QUERY was
       refused (mod_need found no file) and it took kern_small's path;
    C  the module is NOT held: mod_tab's MOD_DESK row is 0 and every DKDFP
       slot still names mod_gone;
    D  its row is KEPT: still the Sticky App row (+3 = 'D'), with no desktop cell -
       a refused QUERY is not a missing target, and nothing downgrades it;
    E  THE VOLUME IS PUT BACK: read at an exec breakpoint on stik_boot's
       RETURN, when every sc_open bracket has closed and the widget has run
       nothing yet, the open bracket's pair (drv_vol_cwd / drv_vol_drv) is
       the boot root and so is the machine - [dsk_cwd] = 0 and [disk_drive] =
       [dsk_bootvol]. The load (twice: the loader's step 5 and the QUERY) ran
       a drv_vol bracket INSIDE sc_open's, which keeps one static pair, and
       without desk_need's save the outer restore left the machine in
       STIKAPPS. It is NOT read once the window
       is up: by then the widget's first tick has registered its row through
       the settings core - a drv_vol bracket of its own, banked wherever the
       machine stands - and its own file I/O has walked to its folder, so
       both name STIKAPPS on any build, which is not sc_open's doing.
  present the same disk with DESK.DRV left on it - the control:
    A  the calendar is up, B a DESK window (WF_DESK, no title bar), C the
       module HELD (the row's segment non-zero, the hover slot not mod_gone),
       D the row kept the same way, E the volume put back the same way, and
    F  the module's 1 KB claim is ABOVE the calendar's region: the loader's
       step 5 took it for an OS88_F_DESK package BEFORE the region, so a
       closed widget's region is not stranded above a pinned island
       (SPEC.md 11.103.10).
  bdrive  `present` with the row on B: (a second copy of the shipped disk in
       the B: drive) - every claim again, and E is the DRIVE half of the
       pair: the boot ends on A:, where it was, and not mounted on B:.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): move stik_boot's loop into
DESK.DRV (the cut design's first draft) and `absent` A fails - no window at
all; drop desk_need's save of drv_vol_cwd / drv_vol_drv and E fails on both
legs; take the loader's step-5 `call desk_need` out and `present` F fails; make osapi_wm_desk_x's QUERY answer CF = 0 without mod_need and
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
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88stik    # noqa: E402
import os88flush   # noqa: E402
import os88geom as geom  # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
DESK_SC0 = os88stik.DESK_SC0
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
    vol = "B" if leg == "bdrive" else "A"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88stik.py"),
                    "cfg", "-o", cfg, "--stik",
                    "%s:STIKAPPS/DESKCAL.O88=Calendar" % vol], check=True,
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
    bimg = None
    if name == "bdrive":        # B: is the shipped system disk again, so its
        bimg = os.path.join(tmp, "nodesk-b.img")    # STIKAPPS\ holds
        shutil.copy(os.path.join(BDIR, "os8088-360.img"), bimg)  # DESKCAL
    print("%s:" % name)
    with os88marty.launch(img, apps=bimg,
                          machine=os88marty.machine("os8088_xt_vga", None),
                          boot=False) as m:
        # E's reading is taken FIRST, at the one instant it means anything:
        # stik_boot's return, when every sc_open bracket has closed and the
        # widget has run nothing yet. Read after the window is up, it is not
        # sc_open's result any more - the widget's first tick registers its
        # row through the settings core (os88stik.inc's stik_beat: mod_need
        # MOD_SETS, a drv_vol bracket of its own, banked wherever the machine
        # stands), and its state-file save walks to its own folder, so
        # [dsk_cwd] and the pair both name STIKAPPS on any build. That was
        # this row's first E, and it failed for that reason on all three legs
        m.bp_exec("stik_boot")
        m.run()
        hit = m.wait_stop(120)
        ret = None
        if hit == "breakpoint":
            r = m.regs()
            ret = (r["cs"] << 4) + struct.unpack(
                "<H", bytes(m.read((r["ss"] << 4) + r["sp"], 2)))[0]
            m.bp_exec(ret)
            m.run()
            hit = m.wait_stop(120)
        sym = m.sym
        boot = m.read(sym("dsk_bootvol"), 1)[0]
        at_ret = hit == "breakpoint" and ret is not None
        pcwd = struct.unpack("<H", bytes(m.read(sym("drv_vol_cwd"), 2)))[0]
        pdrv = m.read(sym("drv_vol_drv"), 1)[0]
        cwd = struct.unpack("<H", bytes(m.read(sym("dsk_cwd"), 2)))[0]
        drv = m.read(sym("disk_drive"), 1)[0]
        m.bp_exec()
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=180.0)
        os88marty.no_saver(m)
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
        r = os88stik.parse_row(bytes(m.read(sseg << 4, os88stik.SC_REC)))
        zb = m.read(ui._S("desk_zslot") + DESK_SC0, 1)[0]
        check(r["stik"] and r["kind"] == 1 and zb == 0xFF,
              "still the Sticky App row (+3 = %#x), with no cell (%#x)"
              % (r["mark"], zb))
        print(" E: the volume is put back")
        check(at_ret, "stik_boot was entered and returned (%r)" % hit)
        check(pcwd == 0 and pdrv == boot,
              "at stik_boot's return the open bracket's pair is the boot "
              "root: drv_vol_cwd %#x, drv_vol_drv %d (boot %d)"
              % (pcwd, pdrv, boot))
        check(cwd == 0 and drv == boot,
              "...and so is the machine: dsk_cwd %#x, disk_drive %d (boot %d)"
              % (cwd, drv, boot))
        if not absent:
            print(" F: DESK.DRV sits above the widget's region")
            rec = ui._S("wm_wins") + w.i * geom.WIN_SIZE
            wseg = struct.unpack("<H", bytes(m.read(rec + geom.W_SEG, 2)))[0]
            check(seg > wseg, "the module at %#06x, the calendar's region at "
                  "%#06x" % (seg, wseg))


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    tmp = tempfile.mkdtemp(prefix="stiknodesk-")
    try:
        for name in ("absent", "present", "bdrive"):
            if only in (None, name):
                leg(name, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("stiknodesk: %s" % ("PASS" if not fails else
                             "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
