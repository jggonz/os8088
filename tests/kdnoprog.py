#!/usr/bin/env python3
"""A PROGRAM THAT IS NOT THERE, ON ARM 1, SAYS SO (SPEC.md 96.40.7).

    python3 tests/kdnoprog.py [--machine os8088_5150_cga_hdd]

Field report: *"trying to run a program that doesn't exist, via typing it in
the text box and clicking run with shut down the os checked, does not give any
message"*.  What it really said was worse than nothing:

    Starting C:\\NOSUCH.COM
    C:\\NOSUCH.COM ended, exit code 255 (Arena: 597KB)

The machine handed over, `kern_dos` could not open the file, and came home with
255 - which the box printed as **a program that ran and exited 255**.  A
refusal wearing a result's clothes is worse than silence: `[dos_state]` read
`DST_RAN`, `[dos_err]` read 0, and there is nothing in that line to act on.

**IT HAS TO BE ARM 1 AND IT HAS TO BE A REAL ROUND TRIP.**  Arm 0 has always
reported this correctly - `dos_load` fails inside `dos_run` and `.freeerr`
sets `[dos_err]` - and the console door says `Bad command or file name`.  The
silence is arm 1's specifically: `dos_run` posts the handoff and leaves by
`.outq`, the QUIET door (96.35.1), which exists so a successful post does not
print an exit line for a program that has not started, and which therefore
skips `dos_con_ended` entirely.  So the only thing that can answer this
question is a machine that actually hibernates, boots `kern_dos`, fails, and
comes back - which is why this row needs a FIXED DISK and cannot be faked on a
floppy.

WHAT IT ASSERTS, and each is a separate thing that can be missing:

  1 the box reaches `DST_ERR` and not `DST_RAN`.  This is the whole defect:
    255 is a legal `INT 21h AH=4Ch` code, so a run that returns it and a
    refusal that never ran are indistinguishable unless something else carries
    the difference.
  2 `[dos_err]` is `DER_READ`.  Not merely "an error" - `kd_entry` has FIVE
    refusal arms and they all reported one value, so a row that accepted any
    non-zero would pass on the day they collapse together again.
  3 ...and the sentence is on the GLASS, read out of the console band, naming
    the program.  The state bytes can be right while nothing is printed:
    `dos_con_ended`'s `.err` path is what turns them into words.

**IT READS STATE AND THEN PIXELS**, in that order, because the state is the
contract and the glass is the consequence.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88build                                               # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
VHD = "build/kdnoprog.vhd"
CFG = "build/kdnoprog-cfg-%d.bin" % os.getpid()
# ...resolved through os88marty.machine(), NOT named bare: the ROM
# `os8088_5150_cga_hdd` asks for is IBM's and is not in this tree
# (CONTRIBUTING.md 6), so MartyPC's pinned build exits at once with `ROM set
# ibm5150_82_v4 not found` - which is how this row failed the soak, before its
# first assertion. machine() answers the GLaBIOS twin always, so the row
# behaves the same on a contributor's box and on one with the ROM staged.
MACHINE = M.machine("os8088_5150_cga_hdd")
GONE = "C:\\NOSUCH.COM"                 # ...and it really is not there: the
                                        # fixture below writes four files and
                                        # this is not one of them
HDD_CFGBIT = 1                          # kernel/driver.inc's drv_cfgbit, row 1
DST_RAN, DST_ERR = 2, 3                 # apps/dos/dos.asm's own
DER_READ = 2                            # ...and the reason kern_dos sends


def fail(msg):
    print("kdnoprog: FAIL: %s" % msg)
    sys.exit(1)


def at(p):
    """A build/ path in THIS run's tree (docs/WRITING-TESTS.md 62).

    Everything here is written rather than read, so none of it is resolved for
    us the way `launch`'s images are.
    """
    return os88build.at(p)


def fixture():
    """The hard disk, and the SYSTEM.CFG that asks for its driver.

    dosram.py's, less the DOS program: this row is about a name that resolves
    to nothing, so the volume deliberately carries no `.COM` at all.
    """
    for p in (at(TEMPLATE), at("build/kernel.sys"),
              at("build/kdos/DOS.O88")):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the DOS pieces and "
                 "`make marty` the template" % p)
    with open(at(CFG), "wb") as f:
        # kdreturn's eighteen bytes: signature, generation, one DW record and
        # the terminator. NOTHING LOADS UNLESS SYSTEM.CFG ASKS (SPEC.md 51.3),
        # and the machine boots off the fixed disk either way - the boot
        # partition is a DVK_BIOS row served by int 13h (18.7.1) - so without
        # this the row would run on a machine it did not mean to build.
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                + b"DW" + bytes([1, 2])
                + (1 << HDD_CFGBIT).to_bytes(2, "little") + b"\0\0")
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", at(TEMPLATE),
         "--out", at(VHD), "--kernel", at("build/kernel.sys"),
         "--vbr", at("build/boothd.bin"), "--mbr", at("build/mbr.bin"),
         "--file", "HIBER.DRV=" + at("build/hiber.drv"),
         "--file", "CTRL.DRV=" + at("build/ctrl.drv"),
         "--file", "HDD.DRV=" + at("build/hdd.drv"),
         "--file", "DOS.O88=" + at("build/kdos/DOS.O88"),
         "--file", "SYSTEM.CFG=" + at(CFG)], cwd=ROOT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()
    fixture()

    m = M.launch(None, machine=a.machine,
                 extra=["--mount", "hd:0:" + os.path.abspath(at(VHD))])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        mo = os88mouse.Mouse(marty=m)
        if not ui.path("C:/DOS.O88"):
            fail("double-clicking DOS.O88 on the fixed disk opened no window")
        M.settle(m)
        dm = dosmap.package()

        def pb():
            return dosmap.instance(m) << 4

        def rect(n):
            return [int.from_bytes(m.read(pb() + dm[n] + i * 2, 2), "little")
                    for i in range(4)]

        def ctr(n):
            r = rect(n)
            return (r[0] + r[2]) // 2, (r[1] + r[3]) // 2

        def byte(n):
            return m.read(pb() + dm[n], 1)[0]

        # --- a name that resolves and opens nothing, then arm 1, then Run ----
        # The command box is `dos_pln` and its rect is composed at paint time,
        # so this is a position out of the guest rather than a remembered one.
        mo.click(*ctr("dos_pln"))
        M.settle(m)
        m.type_text(GONE)
        M.settle(m)
        got = m.read(pb() + dm["dos_path"], 24).split(b"\0")[0].decode("latin-1")
        if got.upper() != GONE:
            fail("the command box holds %r and %r was typed" % (got, GONE))
        # **THE ARM IS POKED AND THE RUN IS CLICKED.** Driving the radio is
        # tests/dosmem.py's subject and re-driving it here would make this row
        # fail for somebody else's defect; the RUN has to be a real press,
        # because the quiet door under test is on that path.
        m.write(pb() + dm["dos_keepc"], bytes([1, 0]))
        mo.click(*ctr("dos_rrect"))

        # --- 1: it comes back, and as a REFUSAL --------------------------
        # **THE WAIT IS ON THE CONSOLE AND NOT ON `[dos_state]`**, which cost
        # this row its first run. `dos_go` sets DST_RAN *before* calling
        # `dos_run`, and on arm 1 `dos_run` posts and leaves by the quiet door
        # without touching it - so DST_RAN is the IN-FLIGHT state here, not a
        # terminal one, and a predicate that accepts it fires before the
        # machine has even hibernated. The console cannot be confused that way:
        # a second line naming the program is the outcome, by construction.
        def band():
            scr = m.read(pb() + dm["con_scr"], 80 * 25 * 2)
            rows = ["".join(chr(scr[r * 160 + i])
                            if 32 <= scr[r * 160 + i] < 127 else " "
                            for i in range(0, 160, 2)).rstrip()
                    for r in range(25)]
            return [r for r in rows if r.strip()]

        def answered(_=None):
            # `Starting <path>` is line one (96.33.20.1); the RESULT is a
            # second line naming it, whichever way it went.
            return len([r for r in band() if "NOSUCH" in r.upper()]) >= 2

        M.until(m, answered, "the box to come back and say what happened",
                limit=300.0, guest=900.0)
        st, er = byte("dos_state"), byte("dos_err")
        print("kdnoprog: the box came back with state=%d err=%d" % (st, er))
        # **THE BAND, BEFORE ANY VERDICT.** A failure here is about what the
        # machine SAID, so printing it after the first `fail()` would be
        # printing it never - and the whole report this row exists for was
        # about a message that did not arrive.
        live = band()
        for r in live[-5:]:
            print("kdnoprog:   | %s" % r)
        if st == DST_RAN:
            fail("the box is at DST_RAN for a program that was never on the "
                 "disk, so the console says `ended, exit code 255` about a run "
                 "that did not happen. 255 is a legal INT 21h AH=4Ch code, so "
                 "kern_dos has to carry the REASON home - KDC_FAIL in "
                 "KDH_CODE's high byte (SPEC.md 96.40.7)")
        if st != DST_ERR:
            fail("the box is at state %d, which is neither DST_RAN nor "
                 "DST_ERR - the handoff never came back at all" % st)

        # --- 2: ...and it is the RIGHT refusal ------------------------------
        if er != DER_READ:
            fail("the box reports DER_%d and the program could not be READ "
                 "(DER_READ = %d). kd_entry has five refusal arms and they "
                 "used to report ONE value between them (SPEC.md 96.40.7), so "
                 "a row that took any non-zero here would pass on the day they "
                 "collapse together again" % (er, DER_READ))
        print("kdnoprog: ...and the reason is DER_READ, not a code")

        # --- 3: ...and it reached the GLASS ---------------------------------
        # The state bytes can be right while nothing is printed: it is
        # dos_con_ended's `.err` path that turns them into words, and this row
        # exists because a message that never arrived was the report. Read out
        # of `con_scr`, the console's own 80x25 buffer, which is doscon.py's
        # reader - the digits on the band are a font and this is the text.
        said = [r for r in live if "NOSUCH" in r.upper()]
        if not said:
            fail("the console never names the program at all. The state bytes "
                 "are right, so dos_con_ended's `.err` path did not run - "
                 "which is the ORIGINAL report (`does not give any message`) "
                 "with the cause one layer further in. The band holds: %r"
                 % (live[-4:],))
        if not any(":" in r for r in said):
            fail("the console names the program and says nothing about it: "
                 "%r. dos_con_ended's `.err` arm prints the path, then `: `, "
                 "then dos_err_line's sentence (SPEC.md 96.33.20)" % (said,))
        print("kdnoprog: ...and the console says %r" % (said[-1],))
    finally:
        m.close()
    print("kdnoprog: ok - a name that opens nothing is a REFUSAL and says so")
    return 0


if __name__ == "__main__":
    sys.exit(main())
