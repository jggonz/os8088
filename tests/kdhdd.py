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
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-o", prog,
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
    """
    end = time.time() + secs
    while time.time() < end:
        rs = rows(m)
        if any(want in r for r in rs):
            return rs
        for r in rs:
            if "could not mount" in r:
                fail("%s: %s. THE VOLUME IS NOT IN kern_dos's TABLE - that "
                     "is disk.inc's static dsk_vtab, whose row 2 is DVK_FREE "
                     "and is where SPEC.md 18.7.1 pins the boot partition "
                     "(96.46)" % (what, r.strip()))
        time.sleep(0.25)
    fail("%s: %r never reached the text screen; the last one held %r"
         % (what, want, [r for r in rows(m) if r.strip()][:10]))


def marker(rs):
    for r in rs:
        if "file says:" in r:
            return r.split("file says:", 1)[1].strip()
    return None


def arm3_run(m, mo):
    """Set the Memory page's the Shut down the OS arm and press Run.

    Poked rather than clicked, which `tests/kdreturn.py` does for the same
    reason: the radio is `tests/kdmouse.py`'s and `tests/kdhand.py`'s subject
    and re-driving it here would make this row fail for somebody else's
    defect.
    """
    dm = dosmap.package()
    pseg = dosmap.instance(m)
    m.write((pseg << 4) + dm["dos_keepc"], bytes([1, 0]))
    mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))


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
        # `kern_dos` holds its screen (`kd_leave` waits on a key), which is
        # why the arm that matters is reliable. What the association buys
        # here is the path box already filled, not an assertion.
        M.settle(m)
        got = marker(rows(m))
        if got:
            print("kdhdd: windowed off C:, the file says %r" % got)

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
