#!/usr/bin/env python3
"""Hibernate to the hard disk, and come back (SPEC.md 87).

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
import os88build as _B
import os88marty as M                                     # noqa: E402
from harness import check, done                           # noqa: E402

RUN = "build/martypc/run"
TEMPLATE = os.path.join(RUN, "media/hdds/default_xtide.vhd")
# **THIS PROCESS'S OWN DISKS, and that is the whole of why this row was
# flaky.** They were `build/hiber.vhd` and `build/hiber360.img`, fixed paths,
# rebuilt by `fixture()` at the start of every run - and `os88marty.launch`
# copies FLOPPIES into the instance's run directory but passes `--mount
# hd:0:<path>` straight through, so three concurrent runs mounted ONE hard
# disk read-write in three emulators while each rewrote it from the template
# underneath the others.
#
# Rated at a lane of three it failed 2 runs in 6, and the reason it took three
# rounds to find is that it fails at a DIFFERENT LEG every time - whichever
# step happens to collide. All three of these are the same collision:
#
#   - the first boot never reaches a desktop, because another run's
#     HIBERNAT.PTR is on the disk and the machine asks the resume question
#     instead of showing one;
#   - `both files reached the disk` is false, because another run rebuilt the
#     volume from the template between the write and the read;
#   - the [Resume] click does nothing, with the machine ALIVE, nothing locked,
#     the pointer proven on the button and mouse_btn proven decoded - because
#     the image it is being asked to resume from is not the one this machine
#     wrote.
#
# The last of those is what makes a fixed path worth this comment rather than
# a one-line fix: it reads exactly like a product defect in the window's hit
# test, and the row's own header records that the hit test HAS been broken
# here before. docs/WRITING-TESTS.md 5 is the rule one level up - a row may
# not write a shared path - and a `--mount` is the case that rule's own
# wording does not cover, because the runner is not `make`.
_TAG = os.getpid()
VHD = os.path.abspath(_B.at("build/hiber-%d.vhd" % _TAG))
FLOPPY = _B.at("build/hiber360-%d.img" % _TAG)
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


# **THE FILE, NOT THE IMAGE** (docs/plans/O88-COMPRESSION-PLAN.md). `kernel.bin`
# is what the kernel IS and `kernel.sys` is what goes on a volume; since
# `PKGZ ?= lz4` they are different bytes and different lengths, and both
# fixtures below name a kernel by hand where every Makefile rule names
# `$(KERNFILE)`. Getting it wrong is silent at build time and fatal at boot:
# the VHD carried 208 sectors of unpacked image under a boot record built for
# the packed one, and the machine reached a loading screen and stopped there
# for the whole 360-second budget. `tools/os88hdd.py` refuses it now, and this
# is the caller that taught it to.
KERNEL = "build/kernel.sys"


def fixture():
    """The VHD, rebuilt from the tree every run: KERNEL.SYS plus the three
    modules and the driver the boot volume must carry (SPEC.md 2.8.4)."""
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", _B.at(KERNEL), "--vbr", _B.at("build/boothd.bin"),
         "--mbr", _B.at("build/mbr.bin"),
         "--file", "HIBER.DRV=" + _B.at("build/hiber.drv"),
         "--file", "CTRL.DRV=" + _B.at("build/ctrl.drv"),
         "--file", "HDD.DRV=" + _B.at("build/hdd.drv")])


def floppy():
    """A 360KB system disk whose SYSTEM.CFG wants HDD.DRV (row 1 = bit 1),
    the Makefile's ethertest shape."""
    d = _B.at("build/hibcfg-%d" % _TAG)      # per-process, like the disks
    os.makedirs(d, exist_ok=True)
    cfg = os.path.join(d, "system.cfg")      # its basename IS the file's name
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW"
                + bytes([1, 2]) + (1 << 1).to_bytes(2, "little") + b"\0\0")
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", FLOPPY, "--size", "360",
         "--boot", _B.at("build/boot360.bin"), "--kernel", _B.at(KERNEL),
         _B.at("build/hdd.drv"), _B.at("build/hiber.drv"),
         _B.at("build/ctrl.drv"), _B.at("build/format.drv"),
         _B.at("build/clone.drv"), cfg])


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
        M.write_png_rgb("build/hiber-%d-%s.png" % (_TAG, name), 640, 200,
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


def wrote(names, secs=20.0):
    """Wait for NAMES to appear in the VHD's root, read on the HOST.

    **THE GUEST HAVING WRITTEN IS NOT THE HOST BEING ABLE TO READ IT.** Every
    other reading in this row comes out of guest memory over the debug
    socket, which is exact the instant it is taken; these two come out of the
    VHD FILE, which the emulator is holding open and writes to on its own
    schedule. The guest's `int 13h` has returned and the sectors are in
    MartyPC's hands - when they reach the host's filesystem is not something
    the guest can be asked about.

    Read once, that is a race, and at a lane of three it is a LOST one: the
    check `both files reached the disk` failed 1 run in 6 on a machine that
    had written both files perfectly (the run after it resumed from them).
    docs/WRITING-TESTS.md 7.1 - wait for the thing rather than for a clock -
    with the twist that the thing here is not the guest's, so the budget is
    the HOST's and there is nothing else it could be.
    """
    end = time.time() + secs
    while True:
        have = [n for n in names if root_has(n)]
        if len(have) == len(names):
            return True, have
        if time.time() >= end:
            return False, have
        time.sleep(0.5)


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
    ok, have = wrote(["HIBERNAT.PTR", "HIBERNAT.IMG"])
    check(ok, "both files reached the disk (the positive control for the "
              "'gone' checks below)", got=have,
          want=["HIBERNAT.PTR", "HIBERNAT.IMG"])


def _diag(m, mo):
    """Everything that decides whether a PROVEN click could have been acted on.

    `os88mouse.click` proves both halves of itself - `to` raises if the
    published pointer never reaches the target, `_edge` raises if the guest's
    own `mouse_btn` never agrees - so a click that returns and changes nothing
    is not a lost packet. What is left is where the click LANDED and whether
    the machine could act on it, and this is that list.
    """
    try:
        wins = [w for k, w in instances(m) if k == KIND_HIBER]
        if wins:
            r = m.readseg(0x0060, wins[0], 10)
            rect = tuple(r[i] | (r[i + 1] << 8) for i in (2, 4, 6, 8))
            print("      KIND_HIBER window x=%d y=%d w=%d h=%d, button1 at %s"
                  % (rect + (button1(m),)))
        print("      pointer now %s, mouse_btn=%02x"
              % (mo.where()[:2], mo.where()[2]))
        t0 = mo.ticks()
        time.sleep(1.0)
        print("      BIOS ticks +%d in a host second (0 = the machine is "
              "stopped)" % (mo.ticks() - t0))
        for n in ("sch_lock", "gfx_lock_flag", "gfx_lock_own",
                  "gfx_lock_want", "ui_rebootq"):
            try:
                print("      %-14s %d" % (n, byte(m, n)))
            except Exception:
                pass
        v = m.video()
        print("      video %s" % {k: v[k] for k in
                                  ("field_w", "field_h", "mode", "text")
                                  if k in v})
        # THE DISCRIMINATOR. A click that is proven at the pointer and at
        # mouse_btn and still does nothing is either a machine that dispatches
        # no input at all, or a click path that specifically does not reach
        # this button - and the row's own header records that the second has
        # happened here before ("its hit test once clobbered the y and no
        # click ever landed - only Enter worked"). The keyboard answers the
        # same window, so this tells the two apart in one press.
        before = word(m, "hb_resumes")
        m.key("Enter")
        for _ in range(20):
            if word(m, "hb_resumes") != before:
                print("      *** ENTER WORKED where the click did not: the "
                      "CLICK path is the defect, not the machine ***")
                return
            time.sleep(0.5)
        print("      Enter did nothing either: the machine dispatches no "
              "input at all")
    except Exception as e:
        print("      (diagnostic failed: %s)" % e)


def answer(m, mo, what, done):
    """Click the Hibernate window's first button, and say what happened.

    THE SAME SHAPE AS `restart` AND FOR THE SAME REASON. A single unconfirmed
    input carrying the rest of the row is how this one fails: the machine
    sits at the question, nothing this row waits for can ever come true, and
    the message that comes out names a 720-second budget rather than a lost
    click.

    A second click is only right while the window is STILL THERE - a machine
    that has taken the first one and is reading its image back has no button
    under the pointer, and clicking the desktop it is about to restore is not
    a retry, it is a new event. So the retry is gated on the KIND_HIBER
    instance still being live, which is also what tells the two apart in the
    report.
    """
    for attempt in (1, 2):
        mo.click(*button1(m))
        try:
            M.until(m, done, what, poll=0.5, limit=60 if attempt == 1 else 240)
            if attempt == 2:
                print("   the [Resume] click needed sending TWICE")
            return
        except M.MartyError:
            live = [k for k, _ in instances(m) if k == KIND_HIBER]
            print("   answer attempt %d: %s - hb_mode=%d, hb_resumes=%d, "
                  "KIND_HIBER live=%s" % (attempt, what, byte(m, "hb_mode"),
                                          word(m, "hb_resumes"), bool(live)))
            _diag(m, mo)
            shot(m, "answer%d" % attempt)
            if attempt == 2 or not live:
                raise               # the window is gone: it TOOK the click
                                    # and is working, so a second one would
                                    # land on whatever replaced it


def restart(m):
    """The key that restarts a hibernated machine, and the wait it earns.

    IT WAS `m.key("Space"); time.sleep(1.0)` and then one 240-second wait for
    [hb_mode], which is two of docs/WRITING-TESTS.md 7's mistakes in three
    lines: a host sleep before an event the guest has to reach, and a single
    unconfirmed keystroke carrying the whole rest of the row. Rated at a lane
    of three it failed 3 runs in 6, every one of them here, having burned 720
    GUEST seconds - twelve emulated minutes for a reboot that takes four,
    which is not a slow machine, it is a machine that was never going to get
    there.

    So the key is CONFIRMED, in the only way it can be: the machine has to
    leave the sentence. `hbm_kbdrain` empties the ROM's buffer before the
    sentence is printed (SPEC.md 87.4 step 7), so a key that arrives in the
    window between the drain and `int 0x16` is the one that can be dropped -
    and dropping it parks the guest in the sentence for ever, where nothing
    this row waits for will ever come true. A second press costs nothing when
    the first was taken (the machine has already left the buffer behind), and
    it is the whole of the failure when it was not.

    On the way out it says which happened, because "the key needed sending
    twice" is a fact about this emulator's keyboard that the next reader of a
    720-second timeout should not have to re-derive.
    """
    for attempt in (1, 2):
        m.key("Space")
        try:
            M.until(m, lambda mm: byte(mm, "hb_mode") == HB_M_RESUME,
                    "the fresh boot to ask the question", poll=1.0,
                    limit=60 if attempt == 1 else 240)
            if attempt == 2:
                print("   the restart key needed sending TWICE (the first "
                      "landed between hbm_kbdrain and int 16h)")
            return
        except M.MartyError:
            v = m.video()
            print("   restart attempt %d: no question yet - video %s, "
                  "text=%s, hb_mode=%d, hb_resumes=%d"
                  % (attempt, v.get("mode", v.get("field_w")),
                     M.video_is_text(v), byte(m, "hb_mode"),
                     word(m, "hb_resumes")))
            shot(m, "restart%d" % attempt)
            if attempt == 2 or not M.video_is_text(v):
                raise               # not the sentence: a second key is not
                                    # the answer and would only hide it


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
        restart(m)
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
        answer(m, mo, "[hb_resumes] to reach 1",
               lambda mm: word(mm, "hb_resumes") == 1)
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
    check(not img_present(), "...and so is HIBERNAT.IMG (SPEC.md 87.6 step 5)")


def pass_discard(driver):
    with boot(driver) as m:
        from os88mouse import Mouse
        mo = Mouse(marty=m)
        mo.menu(CHIP_X, CHIP_Y, CHIP_X, ABOUT_Y)
        quiet(m)
        hibernate(m, mo, mouse=False)
        restart(m)
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
    try:
        fixture()
        if a.driver:
            floppy()
        if a.only != "discard":
            pass_resume(a.driver)
        if a.only != "resume":
            pass_discard(a.driver)
        done("hibernate" + (" (driver)" if a.driver else ""))
    finally:
        sweep()


def sweep():
    """Take this process's disks away again.

    A 32MB VHD per run is not something to leave in build/ - and a per-PROCESS
    name cannot be reclaimed by the next run the way a fixed one was, since
    pid_max wraps in minutes on a busy box and the next owner of this number
    is somebody else. The screenshots stay: they are the only thing here worth
    reading after the fact, and they are kilobytes.
    """
    import shutil
    for p in (VHD, FLOPPY):
        try:
            os.unlink(p)
        except OSError:
            pass
    shutil.rmtree(_B.at("build/hibcfg-%d" % _TAG), ignore_errors=True)


if __name__ == "__main__":
    main()
