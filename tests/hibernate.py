#!/usr/bin/env python3
"""Hibernate to the hard disk, and come back (SPEC.md 86).

    python3 tests/hibernate.py            # the boot partition: DVK_BIOS
    python3 tests/hibernate.py --driver   # ...and through HDD.DRV: DVK_DRV

A fixture volume out of tools/os88hdd.py, with HIBER.DRV, CTRL.DRV and
HDD.DRV in its root, on MartyPC's os8088_xt_hdd - XT-IDE's option ROM, which
is rung 0 (SPEC.md 52.1), the transport the field machine has and the only
one the resume stub speaks.

Two passes, and each ASSERTS out of the guest's memory rather than out of a
screenshot, because the thing under test is that memory:

  1. RESUME. An About box is opened as the witness. Hibernate... is picked
     from the System menu and the Hibernate window's first button is CLICKED
     WITH THE MOUSE (the release path: its hit test once clobbered the y and
     no click ever landed - only Enter worked), the machine writes the image
     to the boot disk's root without asking where, and reaches the ROM's text
     mode with its sentence; a key restarts it. The fresh desktop must carry
     the question ([hb_mode] = HB_M_RESUME and a live KIND_HIBER instance), a
     CLICK on Resume must answer it, and the desktop that comes back must be
     the OLD one: [hb_resumes] = 1, the About instance alive with its window,
     no Hibernate window left, and HIBERNAT.PTR gone from the volume.
  2. DISCARD. The same again through the KEYBOARD - Enter to hibernate, Esc at
     the question: the desktop is a fresh boot's - no About, [hb_resumes] = 0
     - and the pointer is gone all the same.

--driver boots the same VHD from a 360KB floppy whose SYSTEM.CFG already wants
HDD.DRV (the ethertest shape), so C: is a DRIVER volume and the transport
facts come through DSV_GEOM rather than out of the kernel's own table.

REQUIRES: build/martypc (docs/MARTYPC-DEBUG.md). Writes only under build/.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
sys.path.insert(0, "tests")
import os88marty as M                                     # noqa: E402
from harness import check, done                           # noqa: E402

RUN = "build/martypc/run"
TEMPLATE = os.path.join(RUN, "media/hdds/default_xtide.vhd")
VHD = os.path.abspath("build/hiber.vhd")
FLOPPY = "build/hiber360.img"
MACHINE = "os8088_xt_hdd"

# kernel/hiber.inc, kernel/instance.inc - the module's own constants
HB_M_ASK, HB_M_BUSY, HB_M_RESUME, HB_M_GONE = 0, 1, 2, 3
KIND_ABOUT, KIND_HIBER = 0, 5
I_RECSZ, INST_MAX = 32, 12
I_STATE, I_KIND, I_WIN = 0, 2, 4
W_FLAGS = 0
from os88geom import WIN_SIZE, MAX_WIN                    # noqa: E402

CHIP_X, CHIP_Y = 8, 8               # the System menu's cell
ITEM_Y0, ITEM_H = 24, 16            # MENU_ITEM_H: item n is at 24 + 16n
ABOUT_Y = ITEM_Y0 + 0 * ITEM_H
HIBER_Y = ITEM_Y0 + 4 * ITEM_H      # About, Control Panel, Task Manager,
                                    # the rule, Hibernate..., Restart
# hiber.inc: the content is at W_X+1, W_Y+TITLE_H (18); the first button at
# content +8,+54, 120 x 16 - so its middle is this far from the FRAME, and
# the frame is read out of the window record, because the template's 160,150
# is only where a 480-line screen leaves it: a CGA moves it up to fit
BTN1_DX, BTN1_DY = 1 + 8 + 60, 18 + 54 + 8
W_X, W_Y = 2, 4


def button1(m):
    """Screen coordinates of the Hibernate window's first button."""
    win = [w for k, w in instances(m) if k == KIND_HIBER][0]
    r = m.readseg(0x0060, win, 6)
    x, y = struct.unpack_from("<HH", r, W_X)
    return x + BTN1_DX, y + BTN1_DY


def fixture():
    """The VHD, rebuilt from the tree every run: KERNEL.SYS plus the three
    modules and the driver the boot volume must carry (SPEC.md 2.8.4)."""
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", "build/kernel.bin", "--vbr", "build/boothd.bin",
         "--mbr", "build/mbr.bin",
         "--file", "HIBER.DRV=build/hiber.drv",
         "--file", "CTRL.DRV=build/ctrl.drv",
         "--file", "HDD.DRV=build/hdd.drv"])


def floppy():
    """A 360KB system disk whose SYSTEM.CFG wants HDD.DRV (row 1 = bit 1),
    the Makefile's ethertest shape."""
    os.makedirs("build/hibcfg", exist_ok=True)
    cfg = "build/hibcfg/system.cfg"          # its basename IS the file's name
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW"
                + bytes([1, 2]) + (1 << 1).to_bytes(2, "little") + b"\0\0")
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", FLOPPY, "--size", "360",
         "--boot", "build/boot360.bin", "--kernel", "build/kernel.bin",
         "build/hdd.drv", "build/hiber.drv", "build/ctrl.drv", "build/format.drv",
         "build/clone.drv", cfg])


def byte(m, name):
    return m.read(m.sym(name), 1)[0]


def word(m, name):
    return int.from_bytes(m.read(m.sym(name), 2), "little")


def instances(m):
    """[(kind, win)] of every LIVE instance record."""
    b = m.read(m.sym("inst_tab"), I_RECSZ * INST_MAX)
    out = []
    for i in range(INST_MAX):
        r = b[i * I_RECSZ:(i + 1) * I_RECSZ]
        if r[I_STATE] == 1:
            out.append((r[I_KIND], struct.unpack_from("<H", r, I_WIN)[0]))
    return out


def visible(m, winptr):
    """Is the window at KERNEL offset winptr used and visible?"""
    if not winptr:
        return False
    flags = int.from_bytes(m.readseg(0x0060, winptr, 2), "little")
    return (flags & 3) == 3


def shot(m, name):
    """A rendered screenshot into build/, for the eye (the assertions are
    the memory reads, not this)."""
    try:
        M.write_png_rgb("build/hiber-%s.png" % name, 640, 200,
                        M.crop_rgb(m, 0, 0, 640, 200))
    except Exception as e:                       # a picture is not a check
        print("shot %s: %s" % (name, e))


def quiet(m, s=8.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def root_has(name):
    """Does the VHD's root carry NAME? Read on the host with a FAT reader
    that is not the kernel's."""
    import instdeep
    blob = open(VHD, "rb").read()
    base = int.from_bytes(blob[446 + 8:446 + 12], "little")
    vol = instdeep.Vol(blob[base * 512:])
    return name in [e[0] for e in vol.entries(0)]


def ptr_present():
    return root_has("HIBERNAT.PTR")


def img_present():
    return root_has("HIBERNAT.IMG")


def boot(driver):
    if driver:
        return M.launch(FLOPPY, machine=MACHINE,
                        extra=["--mount", "hd:0:" + VHD])
    return M.launch(None, machine=MACHINE, extra=["--mount", "hd:0:" + VHD])


def hibernate(m, mo, mouse):
    """From a desktop with the witness up to the ROM's text screen, the
    Hibernate button taken with the mouse or with Enter."""
    mo.menu(CHIP_X, CHIP_Y, CHIP_X, HIBER_Y)      # System -> Hibernate...
    quiet(m)
    check(byte(m, "hb_mode") == HB_M_ASK, "the Hibernate window opened",
          got=byte(m, "hb_mode"), want=HB_M_ASK)
    check(any(k == KIND_HIBER for k, _ in instances(m)),
          "a KIND_HIBER instance is live")
    shot(m, "ask")
    if mouse:
        mo.click(*button1(m))                     # [Hibernate]
    else:
        m.key("Enter")
    M.until(m, lambda mm: M.video_is_text(mm.video()),
            "the hibernated sentence in text mode", poll=0.5, limit=240)
    check(ptr_present() and img_present(),
          "both files reached the disk (the positive control for the "
          "'gone' checks below)")


def pass_resume(driver):
    with boot(driver) as m:
        from os88mouse import Mouse
        mo = Mouse(marty=m)
        r0 = word(m, "hb_resumes")
        check(r0 == 0, "a fresh boot has resumed nothing", got=r0, want=0)
        mo.menu(CHIP_X, CHIP_Y, CHIP_X, ABOUT_Y)  # the witness
        quiet(m)
        check(any(k == KIND_ABOUT for k, _ in instances(m)),
              "the About box is up before hibernating")
        hibernate(m, mo, mouse=True)
        m.key("Space")                            # ...and restart
        time.sleep(1.0)
        M.until(m, lambda mm: byte(mm, "hb_mode") == HB_M_RESUME,
                "the fresh boot to ask the question", poll=1.0, limit=240)
        quiet(m)                                  # ...and paint it. Not a
                                                  # settle: the ROM's disk
                                                  # probe holds a static
                                                  # screen long enough to
                                                  # pass for a quiet desktop
        check(byte(m, "hb_mode") == HB_M_RESUME,
              "the fresh boot asks the question",
              got=byte(m, "hb_mode"), want=HB_M_RESUME)
        check(any(k == KIND_HIBER for k, _ in instances(m)),
              "the question is a live KIND_HIBER instance")
        check(not any(k == KIND_ABOUT for k, _ in instances(m)),
              "the fresh boot has no About box yet")
        shot(m, "question")
        mo.click(*button1(m))                     # [Resume], with the mouse
        M.until(m, lambda mm: word(mm, "hb_resumes") == 1,
                "[hb_resumes] to reach 1", poll=0.5, limit=240)
        quiet(m)
        inst = instances(m)
        about = [w for k, w in inst if k == KIND_ABOUT]
        check(len(about) == 1, "the About box came back with the memory",
              got=inst, want="one KIND_ABOUT")
        check(about and visible(m, about[0]), "...and its window is visible")
        check(not any(k == KIND_HIBER for k, _ in inst),
              "the Hibernate window closed itself on waking", got=inst)
        check(byte(m, "hb_mode") == HB_M_GONE, "the module's state is GONE",
              got=byte(m, "hb_mode"), want=HB_M_GONE)
        check(byte(m, "sch_lock") == 0, "sch_lock is down again",
              got=byte(m, "sch_lock"), want=0)
        check(byte(m, "gfx_lock_flag") == 0, "the gfx lock is released",
              got=byte(m, "gfx_lock_flag"), want=0)
        t0 = word(m, "ticks")
        time.sleep(1.5)
        check(word(m, "ticks") != t0, "the tick is running after the resume")
        shot(m, "resumed")
        # the menu bar still answers: open and close the System menu
        mo.menu(CHIP_X, CHIP_Y, CHIP_X, CHIP_Y)
        quiet(m)
    check(not ptr_present(), "HIBERNAT.PTR is gone after a resume")
    check(not img_present(), "...and so is HIBERNAT.IMG (SPEC.md 86.6 step 5)")


def pass_discard(driver):
    with boot(driver) as m:
        from os88mouse import Mouse
        mo = Mouse(marty=m)
        mo.menu(CHIP_X, CHIP_Y, CHIP_X, ABOUT_Y)
        quiet(m)
        hibernate(m, mo, mouse=False)
        m.key("Space")
        time.sleep(1.0)
        M.until(m, lambda mm: byte(mm, "hb_mode") == HB_M_RESUME,
                "the fresh boot to ask the question", poll=1.0, limit=240)
        quiet(m)
        check(byte(m, "hb_mode") == HB_M_RESUME, "the question, again",
              got=byte(m, "hb_mode"), want=HB_M_RESUME)
        m.key("Escape")                           # Discard
        quiet(m)
        inst = instances(m)
        check(not any(k == KIND_HIBER for k, _ in inst),
              "Discard closed the window", got=inst)
        check(not any(k == KIND_ABOUT for k, _ in inst),
              "...and the desktop is the fresh boot's: no About", got=inst)
        check(word(m, "hb_resumes") == 0, "nothing was resumed",
              got=word(m, "hb_resumes"), want=0)
    check(not ptr_present(), "HIBERNAT.PTR is gone after a discard")
    check(not img_present(), "...and so is HIBERNAT.IMG")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--driver", action="store_true",
                    help="boot from a floppy and reach the disk through HDD.DRV")
    ap.add_argument("--only", choices=["resume", "discard"])
    a = ap.parse_args()
    fixture()
    if a.driver:
        floppy()
    if a.only != "discard":
        pass_resume(a.driver)
    if a.only != "resume":
        pass_discard(a.driver)
    done("hibernate" + (" (driver)" if a.driver else ""))


if __name__ == "__main__":
    main()
