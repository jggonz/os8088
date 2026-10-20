#!/usr/bin/env python3
"""SPEC.md 18.100: a Restart leaves the floppy heads ON TRACK 0.

    make && python3 tests/fddpark.py

`int 19h` resets no hardware, so the next boot inherits the FDC exactly as
this session leaves it - and SPEC.md 18.97's probe decides on a LEVEL READ of
drive B's own TRACK 0 line.  A session that touched B: leaves the head where
the last transfer put it, which costs the probe its fast path on every restart
after that (18.97.5's table: 20 ticks against 1) and, when the head is above
cylinder 77, hands it the exact ST0 the probe RETIRES A DRIVE on (18.97.4).
The hard-disk installer's Restart is the worst case of both, because Copy Apps
has just read a whole floppy and the machine reboots off the hard disk.

THE EVIDENCE IS TAKEN BEFORE `int 19h`, and it has to be: the state under test
exists only between the park and the reboot.  Every emulator here starts the
second boot parked anyway - 18.97.4 verified three ways that MartyPC returns
drive 1's cylinder to 0 on the controller reset the BIOS does at boot - so a
row that looked at the second boot would pass on a kernel that parks nothing.
So `ui_rb_go`, the label on `ui_cmd_reboot`'s own `int 0x19`, is the
breakpoint, and ST3 is read THERE, by the host, off the emulated 765.

FOUR READINGS, and the first two are what make the last two mean anything:

  1. **A fresh boot reads TRK0 SET.**  The apparatus check: if unit 1 does not
     answer 0x39 here, nothing below is about this kernel.  It is also the
     figure 18.97.4 recorded off an IBM-ROM 5150, so the emulator is agreeing
     with a field machine.
  2. **Using B: clears it.**  A Disk window on B: and one folder opened inside
     it - real reads, through the real driver - and the same read now answers
     0x29.  This is the DIRT.  Without it the two arms below would be
     comparing two already-parked machines and both would pass.
  3. **The default kernel parks it**: TRK0 back, at `ui_rb_go`.
  4. **`make NOFDDPARK=1` does not**: TRK0 still clear at the same
     instruction, from the same dirtied state.  Reading 3 without 4 says only
     that something somewhere parked the head, which a BIOS or an emulator is
     entitled to do; the pair says it was this code.

WHAT THIS ROW CANNOT SEE is the 77-step half.  A real 765 gives up on
RECALIBRATE after 77 steps with EQUIPMENT CHECK; 18.100 leans on the BIOS's
own retry (the IBM ROM resets and re-recalibrates several times) to cross it,
and MartyPC seeks straight to cylinder 0 and models no such limit, so the
head is at track 0 here whether the retry is exercised or not.  The IBM ROM
and the 765 datasheet are the record for that one.

WHAT 18.100 ACTUALLY DOES is the ROM's `int 13h AH=00h` (RESET DISK SYSTEM =
recalibrate) once per claimed unit, so this test proves the head reaches
track 0, not any particular register poke.  That the BIOS call parks the head
was verified on both the field GLaBIOS and the genuine IBM 5150 27-Oct-82 ROM
by executing one such call in the guest and reading ST3 back; here the whole
`ui_cmd_reboot` path is exercised instead.

ON MARTYPC: the whole claim is about the emulated FDC's own ports, and only
this harness can drive them from outside the guest (docs/MARTYPC-DEBUG.md).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88build                                             # noqa: E402
import os88sym                                               # noqa: E402
import dispcp                                                # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMAGE = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
APPS = os.environ.get("OS88_APPSIMG", "build/apps360.img")
KNOBS = ("NOFDDPARK=1",)                  # the other arm's OWN tree, so a knob
                                          # kernel never lands in build/

UNIT = 1                        # drive B - the one 18.97 contests, and the
                                # one the installer's Copy Apps reads from
FDC_DOR, FDC_MSR, FDC_FIFO = 0x3F2, 0x3F4, 0x3F5
ST3_TRK0 = 0x10
HANDTO = 400                    # host-side MSR spins; each is a debug-server
                                # round trip, so this is seconds, not us
UI_RBQ_NOFLUSH = 2              # ui.inc: restart WITHOUT going near a disk -
                                # what OSAPI_REBOOT AL=1 posts, and what the
                                # installer's Restart posts (SPEC.md 52.10.6)


def say(*a):
    print(*a)
    sys.stdout.flush()


# --- driving the 765 from the HOST -------------------------------------------
#
# The guest is stopped for all of this, so nothing in the kernel is racing us
# and SENSE DRIVE STATUS is a level read that changes no state the guest owns.

def _put(m, b):
    for _ in range(HANDTO):
        s = m.inb(FDC_MSR)
        if s & 0x80:                    # RQM
            if s & 0x40:                # DIO the wrong way: drop what it is
                m.inb(FDC_FIFO)         # offering and look again
                continue
            m.outb(FDC_FIFO, b)
            return True
    return False


def _get(m):
    for _ in range(HANDTO):
        if (m.inb(FDC_MSR) & 0xC0) == 0xC0:
            return m.inb(FDC_FIFO)
    return None


def st3(m, unit=UNIT):
    """SENSE DRIVE STATUS on `unit`, with the DOR selecting it. None = no
    answer, which is a broken apparatus and never a verdict."""
    m.outb(FDC_DOR, 0x0C | unit)        # not-reset + DMA/IRQ, this unit
    if not _put(m, 0x04) or not _put(m, unit):
        return None
    return _get(m)


def trk0(v):
    return None if v is None else bool(v & ST3_TRK0)


def show(what, v):
    say("  %-34s ST3 = %s   TRK0 %s"
        % (what, "??" if v is None else "%02X" % v,
           "??" if v is None else ("SET" if v & ST3_TRK0 else "clear")))
    return v


def leg(defines, label, image=None, apps=None):
    """Boot, dirty drive B, restart, and read ST3 at `ui_rb_go`."""
    S = (lambda n: os88sym.linear(n, defines))
    say("\n=== %s ===" % label)
    with os88marty.launch(image or IMAGE, apps=apps or APPS,
                          machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)

        eqp = m.read(S("fdd_dbg_eqp"), 1)[0]
        m.pause()
        fresh = show("a fresh boot", st3(m))
        m.run()

        # THE DIRT, and it is the real thing: a Disk window on B: and a folder
        # opened inside it, both read through dsk_* and int 13h. A host-driven
        # SEEK would be a shorter route to the same cylinder and would prove
        # less - the claim is about what a SESSION leaves behind.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        win = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, win)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        m.pause()
        dirty = show("after a Disk window read B:", st3(m))

        # THE RESTART. The BYTE and not the menu: ui_reboot_post's whole job is
        # to set it (SPEC.md 20.10), the installer's Restart sets it to exactly
        # this value, and ui_task spends it at the top of its next pass with
        # nothing held. tests/instrest.py owns the click; this row owns the
        # tail, and driving it from the byte keeps a menu's item order out of
        # an FDC test.
        # 18.100's park now ENDS in the int 19h, so on the default arm the
        # GUI's last instruction is dsk_rb_go in .cold; NOFDDPARK=1 keeps
        # ui_rb_go in .text.
        m.bp_exec(S("ui_rb_go" if "NO_FDDPARK" in defines else "dsk_rb_go"))
        m.write(S("ui_rebootq"), bytes([UI_RBQ_NOFLUSH]))
        m.run()
        if not m.wait_stop(60.0):
            say("  !! ui_rb_go was never reached")
            return {"fresh": fresh, "dirty": dirty, "parked": None, "eqp": eqp}
        parked = show("at ui_rb_go, one instruction from int 19h", st3(m))
        return {"fresh": fresh, "dirty": dirty, "parked": parked, "eqp": eqp}


def main(argv):
    os.chdir(ROOT)
    fail = []

    new = leg((), "default (SPEC.md 18.100)")

    if new["eqp"] < 2:
        fail.append("SETUP: the equipment word claims %d floppy drive(s), so "
                    "there is no unit 1 to park and nothing below is a "
                    "verdict. %s is meant to be a two-drive machine."
                    % (new["eqp"], MACHINE))
    if trk0(new["fresh"]) is not True:
        fail.append("SETUP: a fresh boot reads ST3 = %s for unit %d, and "
                    "TRK0 has to be SET there - it is what SPEC.md 18.97.4 "
                    "recorded on an IBM-ROM 5150 (0x39) and what lets the "
                    "probe take its fast path at all. A machine that starts "
                    "off track 0 cannot show this defect or its fix."
                    % ("??" if new["fresh"] is None else "%02X" % new["fresh"],
                       UNIT))
    if trk0(new["dirty"]) is not False:
        fail.append("SETUP: reading B: did not move the head off track 0 "
                    "(ST3 = %s). Without the dirt both arms are comparing "
                    "already-parked machines. Check that the Disk window "
                    "really opened and really listed APPS."
                    % ("??" if new["dirty"] is None else "%02X" % new["dirty"]))

    if not fail and trk0(new["parked"]) is not True:
        fail.append("the head is STILL off track 0 at ui_rb_go (ST3 = %s), so "
                    "int 19h is about to hand the next boot exactly the state "
                    "SPEC.md 18.100 exists to prevent: 18.97's probe misses "
                    "its fast path, and above cylinder 77 it retires drive B "
                    "outright. Check that ui_cmd_reboot still calls "
                    "dsk_fdd_park_x, and that fdd_dbg_eqp is non-zero when it "
                    "does."
                    % ("??" if new["parked"] is None else
                       "%02X" % new["parked"]))

    old = None
    if "--solo" in argv:
        say("\nfddpark: --solo, the NOFDDPARK=1 leg is skipped")
    else:
        say("\n--- building the other arm ---")
        # **THROUGH os88build, NOT A HAND-ROLLED `make BUILD=`** (docs/
        # WRITING-TESTS.md 5.2). The three things it does that this row was
        # doing itself, and one it was not doing at all:
        #
        #   - it picks the directory, and under a frozen soak run that is
        #     inside the RUN's tree rather than the shared `build/fddpark`
        #     this row named literally;
        #   - it passes NO_GATES, so a knob tree does not re-run the size
        #     guards that only describe the shipped kernel;
        #   - it ASKS make what nasm defines the knob compiles to rather than
        #     restating them, which is what `default_defines("NO_FDDPARK")`
        #     was guessing at;
        #   - and it strips the KZ family before the symbol reader sees it.
        #     That last one is why this row failed in the soak with
        #     `boot/boothd.asm:255: error: symbol KZ_HD not defined` - an
        #     error about the boot sector, from a row about the floppy head,
        #     with the arm under test never built.
        t = os88build.tree(*KNOBS, targets=("os8088-360.img", "apps360.img"))
        t.apply()
        try:
            old = leg(t.defines, "NOFDDPARK=1, the kernel before it",
                      image=t.img("os8088-360.img"),
                      apps=t.img("apps360.img"))
        finally:
            os88build.plain().apply()

        if trk0(old["dirty"]) is not False:
            fail.append("SETUP: the NOFDDPARK=1 leg never dirtied the head "
                        "either (ST3 = %s), so the two arms are not "
                        "comparable."
                        % ("??" if old["dirty"] is None else
                           "%02X" % old["dirty"]))
        elif trk0(old["parked"]) is not False:
            fail.append("NOFDDPARK=1 reached ui_rb_go with TRK0 SET (ST3 = "
                        "%s), and it cannot: that arm assembles BYTE FOR BYTE "
                        "identical to the kernel before SPEC.md 18.100, so "
                        "nothing in it touches the FDC on the way out. Either "
                        "the knob is not reaching the build (check KNOBS and "
                        "VIDSTAMP in the Makefile) or these reads are not "
                        "reading what they claim to."
                        % ("??" if old["parked"] is None else
                           "%02X" % old["parked"]))

    say("")
    if fail:
        say("fddpark: %d FAILED" % len(fail))
        for f in fail:
            say("  FAIL: %s" % f)
        return 1
    say("fddpark: fresh %02X -> after B: %02X -> at int 19h %02X%s"
        % (new["fresh"], new["dirty"], new["parked"],
           ("   (NOFDDPARK=1: %02X)" % old["parked"]) if old else ""))
    say("fddpark: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
