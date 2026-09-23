#!/usr/bin/env python3
"""A DOS program reads the FIXED DISK under kern_dos, and reads its OWN drive.

    make kdostest && make marty && python3 tests/kdhdd.py

Two defects with one instrument, and they have different signatures because
the fixture puts a DIFFERENT SIXTEEN-BYTE MARKER in `KDDATA.TXT` on every
volume. `KDHELLO.COM` opens that name with NO DRIVE LETTER, so the marker it
prints back names the drive the open actually landed on.

  - **THE FIXED DISK WAS NOT THERE.** `kern_dos` carried a static `dsk_vtab`
    - A: and B: as BIOS units 0 and 1, every other row free - while os8088
    BUILDS its table at boot and SPEC.md 18.7.1 pins the boot partition at
    ROW 2, which that initialiser has FREE. So the box handed over a volume
    index `kern_dos` read as no volume, and a program on C: could not start.
    `tests/kdreturn.py` said so in its own fixture comment and ran its
    program off a floppy to avoid it.
  - **AND AN INDEX RESOLVED AGAINST THE WRONG TABLE IS ANOTHER DRIVE.** Row 1
    is live in the initialiser and FREE in the kernel on a machine whose
    second floppy SPEC.md 18.97's probe retired, so the same number means
    different volumes on the two sides of the handoff.

SPEC.md 96.46 carries the kernel's live table across in the launch block, with
the fixed disk's `int 13h AH=08h` geometry beside it - `dsk_boot_from_x` asks
that question once at boot and it is in the BOOT OVERLAY, which `kern_dos`
does not have, and the mount that needs the answer is the read that FETCHES
the BPB so it cannot come from there either. Without it the mount keeps the
floppy fallback of 9 sectors and 2 heads and fails honestly.

VERIFIED TO FAIL: with the carry taken out, the run under kern_dos reports
`(open failed)` - the volume is not there at all - and that is a different
picture from a program that opens the wrong drive's copy, which is the whole
point of giving each volume its own marker.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
KERNEL = "build/kernel.sys"
MACHINE = "os8088_xt_hdd"        # kdreturn's machine, for its reason
VHD = os.path.abspath("build/kdhdd-%d.vhd" % os.getpid())
FLOPPY = os.path.abspath("build/kdhdd-%d.img" % os.getpid())
PROG = "KDHELLO.COM"

# SIXTEEN CHARACTERS EXACTLY, because that is what the program reads back: a
# shorter marker would have it printing the newline too and a longer one would
# prove nothing more (tests/kdos.py's own reason).
MARK_C = "fixed-disk-C-yes"
MARK_B = "floppy-B-notthis"
DST_RAN = 2                      # apps/dos/dos.asm's, and dosmap has it as an
                                 # equate - but the two waits below are the
                                 # only readers and a constant here is one
                                 # fewer thing to keep in step


def fail(msg):
    print("kdhdd: FAIL: %s" % msg)
    sys.exit(1)


def rows(m):
    return [r.rstrip() for r in (m.screen() or [])]


def fixture():
    for p in (TEMPLATE, KERNEL, "build/kdos/DOS.O88"):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the DOS pieces and "
                 "`make marty` the template" % p)
    prog = os.path.join(ROOT, "build", PROG)
    # **`-DKDHOLD`: THE PROGRAM HOLDS ITS OWN SCREEN** (SPEC.md 96.49). Both
    # arms below read a marker off the text page, and on THIS machine - which
    # has a fixed disk - nothing else keeps that page up: the windowed run is
    # an fsx bracket that returns to the desktop the instant `AH=4Ch` lands
    # (96.14), and the `kern_dos` run comes back through `kd_resume`, which
    # stages its stub IN the text framebuffer (87.5). `kd_leave`'s old `Press
    # any key to restart` used to hold it and there is no reboot to hold it
    # for any more, so the wait moved into the program - exactly what
    # `tests/doscom/hello.asm` step 5 does and for the same sentence.
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DKDHOLD",
                           "-o", prog,
                           os.path.join(ROOT, "tests/dostrap/kdhello.asm")])
    # **THE FLOPPY'S COPY NEEDS THE REAL NAME ON DISK**: os88disk.py takes
    # `[DIR:]FILE` and has no rename, where os88hdd.py's `--file NAME=path`
    # does - so the two markers go in directories of their own rather than in
    # one directory under two names.
    dc = os.path.join(ROOT, "build", "kdhdd-c", "KDDATA.TXT")
    db = os.path.join(ROOT, "build", "kdhdd-b", "KDDATA.TXT")
    for p, mark in ((dc, MARK_C), (db, MARK_B)):
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write(mark + "\n")
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", KERNEL, "--vbr", "build/boothd.bin",
         "--mbr", "build/mbr.bin",
         "--file", "HIBER.DRV=build/hiber.drv",
         "--file", "CTRL.DRV=build/ctrl.drv",
         "--file", "HDD.DRV=build/hdd.drv",
         "--file", "DOS.O88=build/kdos/DOS.O88",
         "--file", "%s=%s" % (PROG, prog),
         "--file", "KDDATA.TXT=" + dc], cwd=ROOT)
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", FLOPPY, "--size", "360",
         prog, db], cwd=ROOT)


def wait_text(m, want, secs, what):
    """...or the mount's own refusal, which is the defect's LOUD form.

    `kern_dos` says `could not mount drive C (unit none - no such volume)`
    when the row is not there, and waiting the full timeout out against that
    screen turns a precise sentence into `never appeared`. Reading it is what
    makes the row report the volume table rather than the program.

    **THE BUDGET IS THE GUEST'S OWN CLOCK** (docs/plans/SOAK-PARALLEL.md), and
    the reason is not the obvious one. The arm this waits on costs **0.8 host
    seconds and 3.1 guest seconds**, measured, against a budget of 300 - no
    amount of contention turns that into a timeout, so "a loaded box does less
    work" is NOT what was failing here. What a host loop cannot do is ask
    whether the guest is still EXECUTING: `os88marty.until` does, and reports
    a stopped machine in GUEST_STALL seconds naming its CS:IP, where this
    spent the full 300 seconds and then blamed the condition.

    That distinction is the whole value. This row has failed twice in soak
    runs and never once solo in ~20 attempts, always with the same picture -
    the desktop, decoded as text, after the full budget - and a screen that
    has stopped changing because nothing is running looks exactly like one
    that is running and has not got there yet. Only one of those two is worth
    investigating, and the old form could not tell them apart.

    **AND THAT PICTURE WAS THE ANSWER ALL ALONG**: the desktop really was
    back. `KDHELLO.COM` printed every line this row reads, and then exited -
    and with SPEC.md 96.49's live resume there is no `Press any key to
    restart` behind it any more, so `kd_resume` staged its stub over the text
    page microseconds later (87.5) and every poll after that found the
    desktop. Nothing was stuck and nothing was contended; the evidence was
    gone before the first read. `-DKDHOLD` in `fixture` is the fix - the
    program waits on `AH=08h` and the caller types the key - so both waits
    below are now on a page that STANDS rather than on one being raced for.
    """
    def there(mm):
        rs = rows(mm)
        for r in rs:
            if "could not mount" in r:
                fail("%s: %s. THE VOLUME IS NOT IN kern_dos's TABLE - that "
                     "is disk.inc's static dsk_vtab, whose row 2 is DVK_FREE "
                     "and is where SPEC.md 18.7.1 pins the boot partition "
                     "(96.46)" % (what, r.strip()))
        return any(want in r for r in rs)

    try:
        M.until(m, there, what, guest=secs, poll=0.25)
    except M.MartyError as e:
        fail("%s: %r never reached the text screen. %s  The last screen held "
             "%r" % (what, want, str(e).split("\n")[0][:300],
                     [r for r in rows(m) if r.strip()][:10]))
    return rows(m)


def box_state(m):
    """[dos_state], with the INSTANCE SEGMENT RESOLVED ON EVERY READ.

    **A BANKED `pseg` IS A STALE ONE HERE, and it read as an intermittent.**
    The DOS box declares its region movable (SPEC.md 66.6.1.1) and the
    windowed launch this waits on CLAIMS THE ARENA - so `mem_claim` may
    compact and move the region while the wait is running. A base taken
    before `ui.path` returned then names whatever moved into that segment, and
    the wait sits out its whole budget against a number that is nobody's. It
    did exactly that once, and passed on the next run with nothing changed,
    which is the shape every "contention" diagnosis in this suite has worn.

    Resolving each time costs one read of the window table.
    """
    dm = dosmap.package()                   # cached after the first call
    return m.read((dosmap.instance(m) << 4) + dm["dos_state"], 1)[0]


def marker(rs):
    for r in rs:
        if "file says:" in r:
            return r.split("file says:", 1)[1].strip()
    return None


def arm3_run(m, mo):
    """Set the Memory page's the Shut down the OS arm, press Run, and CONFIRM.

    Poked rather than clicked, which `tests/kdreturn.py` does for the same
    reason: the radio is `tests/kdmouse.py`'s and `tests/kdhand.py`'s subject
    and re-driving it here would make this row fail for somebody else's
    defect.

    **THE CONFIRMATION IS NEW AND IT IS WHAT THE ROW WAS MISSING.** This
    clicked Run and returned, so a press that did not take was
    indistinguishable from a handover that began and hung: both leave the
    machine in graphics, and `wait_text` reported the same sentence for
    either. Waiting for the text screen to carry ANYTHING separates them, and
    the failure then names the press rather than the program.

    **IT MUST NOT USE `m.video()`, and that is a measurement rather than a
    preference.** Polling the card's mode is the obvious way to watch for the
    handover, and on the pristine row - with NOTHING else changed - a
    `m.video()` every 250 ms across this window makes it fail 3 times in 3,
    with the exact screen the intermittent produces. `m.screen()` polled 50x
    harder is clean 3 in 3. The card query wedges the guest somewhere in the
    fsx mode change, inside MartyPC, which is pinned upstream - so the rule
    here is to read the SCREEN and leave the card alone.
    """
    dm = dosmap.package()
    pseg = dosmap.instance(m)   # ...RESOLVED HERE, for box_state's reason: a
                                # windowed run has just claimed the arena and
                                # the box's region is movable, so a segment
                                # banked before it is a segment the compaction
                                # may have moved
    m.write((pseg << 4) + dm["dos_keepc"], bytes([1, 0]))
    pt = dosmap.centre(m, pseg, dm, "dos_rrect")
    # THE SCREEN AS IT WAS, because "anything on the text screen" is already
    # TRUE: in graphics mode `screen` decodes the B800 bytes under the
    # desktop, and that is the garbage every failure here has printed. What
    # is quiet while the desktop is up is CHANGE - nothing writes text there
    # - so the handover is the first thing that moves it.
    was = rows(m)
    mo.click(*pt)
    try:
        M.until(m, lambda mm: rows(mm) != was,
                "the handover to write to the text screen", guest=60.0,
                poll=0.25)
    except M.MartyError as e:
        fail("Run was pressed at %r and NOTHING reached the text screen. %s  "
             "The press did not take, or the handover never began - either "
             "way nothing below this could have run, and the volume table "
             "this row is about was never consulted"
             % (pt, str(e).split("\n")[0][:300]))


def main():
    fixture()
    m = M.launch(None, apps=FLOPPY, machine=MACHINE,
                 extra=["--mount", "hd:0:" + VHD])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        mo = os88mouse.Mouse(marty=m)
        print("kdhdd: booted off the fixed disk")

        # --- A. THE PROGRAM IS ON C:, AND SO IS ITS FILE --------------------
        if not ui.path("C:/" + PROG):
            fail("double-clicking %s on the fixed disk opened no window" % PROG)
        # **THE WINDOWED RUN IS DIAGNOSTIC AND NOT ASSERTED**, and that is a
        # correction: the box runs a console program in an fsx bracket that
        # RETURNS TO THE DESKTOP the moment it exits (SPEC.md 96.14), so the
        # text is up for as long as the program takes and no longer - a race
        # this row lost once against a build that could not have changed it.
        # What the association buys here is the path box already filled, not
        # an assertion.
        #
        # **IT IS NO LONGER A RACE, THOUGH, AND THAT IS WHY IT IS READ AT
        # ALL**: with `-DKDHOLD` the program waits, so this reads the same
        # marker the arm below reads, off the same disk, through the box's own
        # back end instead of `kern_dos`'s. Printed rather than asserted,
        # because a windowed defect is `tests/kdos.py`'s subject and a row
        # that fails for somebody else's is a row nobody trusts - but having
        # the two numbers side by side is what makes the one below mean
        # something.
        got = marker(wait_text(m, "KDHELLO done", 150, "the windowed run"))
        print("kdhdd: windowed off C:, the file says %r" % got)
        # ...AND THE KEY THAT RELEASES IT, which is not tidiness: the fsx
        # bracket is still up and the box is still DST_READY, so a click on
        # 'Run' underneath it would be a press the desktop never sees.
        m.type_text("x")
        M.until(m, lambda mm: box_state(mm) == DST_RAN,
                "the windowed run to finish and the desktop to come back",
                guest=60.0, poll=0.25)

        arm3_run(m, mo)
        rs = wait_text(m, "KDHELLO done", 300, "the run under kern_dos off C:")
        got = marker(rs)
        if got == "(open failed)":
            fail("under kern_dos a program on C: could not open its own file: "
                 "the FIXED DISK IS NOT IN THE VOLUME TABLE. That is the "
                 "static dsk_vtab SPEC.md 96.46 replaces - row 2 is DVK_FREE "
                 "in the initialiser and is where 18.7.1 pins the boot "
                 "partition")
        if got != MARK_C:
            fail("under kern_dos a program on C: read %r and its own drive "
                 "holds %r - the volume index resolved against a different "
                 "table (SPEC.md 96.46)" % (got, MARK_C))
        print("kdhdd: UNDER KERN_DOS off C:, the file says %r" % got)
        # **AND THE EXIT IS NOT THIS ROW'S**, which is a correction: it used
        # to wait for `the program has exited` before letting the machine
        # reboot, and since SPEC.md 96.49 there is no reboot - `kd_leave`
        # puts the session back itself and that line never appears. What this
        # row is about is the DRIVE; `tests/kdreturn.py` owns the return.
        #
        # The key is what `-DKDHOLD`'s `AH=08h` is waiting on, so it is the
        # thing that lets the run END - not a leftover from the reboot that
        # used to be here.
        m.type_text("x")

    finally:
        try:
            m.close()
        except Exception:                                      # noqa: BLE001
            pass
        for p in (VHD, FLOPPY):
            try:
                os.remove(p)
            except OSError:
                pass

    print("kdhdd: ok - the fixed disk is a volume under kern_dos, and a "
          "program reads its OWN drive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
