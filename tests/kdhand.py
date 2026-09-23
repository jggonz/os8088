#!/usr/bin/env python3
"""The DOS handoff, end to end (SPEC.md 96.40, docs/plans/KERN-DOS-PLAN.md 7).

Run a .COM in the window, then run THE SAME .COM again with the Memory page's
the Shut down the OS arm picked - and assert that the second run happened on a machine with
no os8088 in it at all: more memory, a text screen the kernel is not drawing,
and a restart at the end that brings the desktop back.

WHAT IT WOULD CATCH, and every one of these was seen FAILING on the way to
writing it (docs/WRITING-TESTS.md 1):

  - the post refused, or spent by nobody        -> the program runs WINDOWED and
                                                   the KB figure does not move
  - the confirmation not asked (SPEC.md 96.42) -> the session goes with no
                                                  warning at all
  - ...or asked and not HONOURED               -> Cancel runs it anyway
  - `hbm_dosrun` reading the record through the -> `mov ds` before `mov si` puts
    poster's DS                                    543 bytes of the package's own
                                                   bss in the record, and the
                                                   magic check refuses it
  - the module asking `dsk_find_name` for a     -> "DOS.O88 is not on that disk
    name in its OWN image                          any more" about a file in the
                                                   folder it just stood in
  - the stub reading the part as a CLASSIC LZ4  -> the T word is read as a token
    block (SPEC.md 20.13.7)                        and the image lands NINE bytes
                                                   along: a jump into rubble
  - `int 1Eh` left naming KERNEL_SEG:dsk_dpt    -> kern_dos's own table is at
                                                   another offset, so the BIOS
                                                   reads code as an EOT: "the
                                                   disk could not be mounted"
  - `.bss` arriving as the outgoing kernel's    -> a volume table that looks
    bytes (nobits is not zeroed by anything)       plausible and is another OS's
  - `OSAPI_MOUSE` surviving into the image      -> `dos_getkey` polls it, so the
                                                   machine spins in the ROM with
                                                   a key already in the ring

It runs on MartyPC and must: the whole point is a real 8088 running a DOS
program with the operating system gone, and both halves are read with
`screen()` off a text mode nothing of ours is driving.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
COM = "build/doscom360.img"
MACH = "os8088_5150_cga_gla"

RD_N, RD_SEL, RD_PITCH, RD_DIS = 10, 12, 14, 16
INOS, WHOLE, NARM = 0, 1, 2          # SPEC.md 96.36.5 took the
                                     # middle arm out: the cache is
                                     # its own control now


def fail(msg):
    print("kdhand: FAIL: %s" % msg)
    sys.exit(1)


def rows(m):
    return [r.rstrip() for r in (m.screen() or [])]


def wait_text(m, want, secs=90, what=""):
    """Wait for `want` on the guest's text screen, or say what was there."""
    end = time.time() + secs
    while time.time() < end:
        rs = rows(m)
        if any(want in r for r in rs):
            return rs
        time.sleep(0.25)
    fail("%s: %r never appeared on the text screen. The last one was %r"
         % (what or want, want, [r for r in rows(m) if r.strip()][:10]))


def topmem(rs, what):
    for r in rs:
        if "Memory to top of block:" in r:
            try:
                return int(r.split(":")[1].strip().split()[0])
            except (IndexError, ValueError):
                fail("could not read a KB figure out of %r" % r)
    fail("%s: the program never printed its top-of-memory figure" % what)


def rec(m, pseg, dm, off):
    return int.from_bytes(m.read((pseg << 4) + dm["dos_mrad"] + off, 2),
                          "little")


# os88ui.inc's alert geometry, mirrored for the same reason tests/dosmem.py
# mirrors the radio's: these are the SDK's numbers and the box does not own
# them. The row is a FORMULA (SPEC.md 75.3) - every button OS88UI_ABW wide - so
# a click lands out of the guest's own window record rather than a remembered
# coordinate.
A_BW, A_BG, A_BH, A_BTNY, TITLE_H, KSEG = 72, 12, 13, 46, 18, 0x0060


def alert_up(m, base, dm):
    """os88ui_awin, read off the guest: the alert's window pointer or 0."""
    return int.from_bytes(m.read(base + dm["os88ui_awin"], 2), "little")


def alert_button(m, base, dm, i, n=2):
    """The middle of button i of n, computed the way os88ui_arect does."""
    w = alert_up(m, base, dm)
    if not w:
        fail("no alert is up to click")
    rec = m.read((KSEG << 4) + w, 8)            # the window record is the
    wx = int.from_bytes(rec[2:4], "little")     # KERNEL's (W_X, W_Y, W_W)
    wy = int.from_bytes(rec[4:6], "little")
    ww = int.from_bytes(rec[6:8], "little")
    row = n * (A_BW + A_BG) - A_BG
    left = wx + (ww - row) // 2 + i * (A_BW + A_BG)
    return left + A_BW // 2, wy + TITLE_H + A_BTNY + A_BH // 2


def launch_whole(ui, program, drive="B:"):
    """Give a DOS program the WHOLE machine, from a fresh desktop.

    The Memory page's third arm (SPEC.md 96.36.1), driven the way
    `tests/kdmouse.py` does it and for its reasons: open the BOX and not the
    program (a program launched by ASSOCIATION is already running in the
    window, and the Memory page cannot be re-armed underneath one), change
    drive FIRST, then name the program with no drive on it (SPEC.md 96.48).

    **THIS EXISTS BECAUSE THE SEQUENCE IS WRITTEN OUT THREE TIMES** - in
    kdmouse, kdmcur and kdmix - and a fourth copy is how a shared sequence
    starts drifting. Those three are deliberately NOT converted here: each is
    a green row and a conversion is a change that has to be re-run, not a
    tidy-up to fold into somebody else's. Convert them when you are next in
    one, rather than adding a fifth copy.

    Raises naming what it saw, so a miss is reported where it happened.
    """
    import dosmap
    import os88mouse

    m = ui.m
    if not ui.path("A:/APPS/DOS.O88"):
        raise RuntimeError("could not open DOS.O88 off the system disk")
    os88marty.settle(m)
    m.type_text(drive + "\n")
    os88marty.settle(m)
    dm = dosmap.package()
    pseg = dosmap.instance(m)
    base = pseg << 4
    mo = os88mouse.Mouse(marty=m)

    mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
    os88marty.settle(m)
    dis = rec(m, pseg, dm, RD_DIS)
    if dis & (1 << WHOLE):
        raise RuntimeError("the Shut down the OS arm is GREYED - this build "
                           "does not carry kern_dos as a part (96.36.1)")
    x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
    pitch = rec(m, pseg, dm, RD_PITCH)
    mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
    os88marty.settle(m)
    if rec(m, pseg, dm, RD_SEL) != WHOLE:
        raise RuntimeError("clicking the Shut down the OS arm did not pick it")
    mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
    os88marty.settle(m)
    mo.click(*dosmap.centre(m, pseg, dm, "dos_pln"))
    os88marty.settle(m)
    m.type_text(program)
    os88marty.settle(m)
    mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
    os88marty.settle(m)
    if alert_up(m, base, dm):
        mo.click(*alert_button(m, base, dm, 1))                # Proceed


def wait_desktop(m, ui, secs=300):
    """Wait for the RESTARTED machine to reach a graphics desktop.

    **`ui.up()` IS NOT THE WAIT HERE**, and neither is poking its two words
    first. A warm boot clears no bss (kernel/hiber.inc's `hb_probe` says so
    about its own three), so `desk_rows` and `menu_nbar` hold whatever is at
    those offsets - which between the handoff and the boot is `kern_dos`'s
    own image, so `up()` returns on the BIOS banner and everything after it
    reads a machine that has not booted. Zeroing them first is worse: at that
    moment those addresses ARE kern_dos's running code.

    The mode is the honest question. os8088's desktop is GRAPHICS on every
    adapter it has (SPEC.md 39) and everything between - the ROM's banner,
    `kern_dos`, the loading screen's own text - is not.

    **ASK IT WITH `video_is_text`**, which knows which of `mode` and `graphics`
    is the live field on each card: on the MDA/Hercules `mode` is dead, because
    os8088 enters HGC graphics through 3BF/3B8 rather than through int 10h, so
    a `"Graphics" in mode` test can never pass there whatever the kernel did.
    This row runs on a colour machine today and the check is still wrong to
    write - tests/kdreturn.py has the same wait and went red on a mono machine
    for exactly this (SPEC.md 96.49.2).
    """
    end = time.time() + secs
    while time.time() < end:
        try:
            if not os88marty.video_is_text(m.video() or {}):
                return ui.ready(limit=secs)
        except Exception:
            pass
        time.sleep(1.0)
    raise RuntimeError("no graphics desktop in %ds; the text screen holds %r"
                       % (secs, [r for r in rows(m) if r.strip()][-4:]))


def main():
    for p in (SYS, COM):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds the system disk and "
                 "`make kdostest` the B: floppy of DOS programs" % p)

    with os88ui.boot(SYS, apps=COM, machine=MACH) as ui:
        m = ui.m

        # --- 1. the ordinary windowed run, which is the BASELINE -------------
        if not ui.path("B:/DOSHELLO.COM"):
            fail("double-clicking DOSHELLO.COM opened no window")
        rs = wait_text(m, "READY", what="the windowed run")
        win_kb = topmem(rs, "the windowed run")
        print("kdhand: windowed, the program has %d KB above its PSP" % win_kb)
        m.type_text("x")
        os88marty.settle(m)
        if "DOS" not in ui.titles():
            fail("the DOS window is gone after the windowed run: %r"
                 % (ui.titles(),))

        dm = dosmap.package()
        pseg = dosmap.instance(m)
        mo = os88mouse.Mouse(marty=m)

        # --- 2. the Shut down the OS arm, which this build can offer --------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_N) != NARM:
            fail("the Memory page has %d arms" % rec(m, pseg, dm, RD_N))
        dis = rec(m, pseg, dm, RD_DIS)
        if dis & (1 << WHOLE):
            fail("the Shut down the OS arm is GREYED on a build that carries kern_dos as "
                 "a part (SPEC.md 96.36.1): OS88UI_RD_DIS is 0x%04X. "
                 "`dos_mem_whole` reads the part table's own length word, so "
                 "either the part is not in DOS.O88 or the row is not found"
                 % dis)
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = rec(m, pseg, dm, RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("clicking the Shut down the OS arm left OS88UI_RD_SEL at %d"
                 % rec(m, pseg, dm, RD_SEL))
        print("kdhand: the Shut down the OS arm is live and picked")

        # --- 3. ...and Run, which does not come back -------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))   # Return, which is
        os88marty.settle(m)                                  # the ONLY way back
                                                             # to the main page:
                                                             # 'Environment' is
                                                             # one-way and Run
                                                             # is only hit there
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)

        # --- 3a. ...WHICH ASKS FIRST (SPEC.md 96.42) -------------------------
        # This machine has no fixed disk, so there is nothing to come back to:
        # every open window goes and the machine restarts when the program
        # exits. That is the one deliberately destructive thing this box does,
        # and it is not allowed to happen quietly.
        base = pseg << 4
        if not alert_up(m, base, dm):
            fail("Run on the Shut down the OS arm went straight to the launch. With no "
                 "fixed disk the session is LOST, and "
                 "docs/plans/KERN-DOS-PLAN.md 9 wants that asked in its own "
                 "window at the moment of launch")
        print("kdhand: the confirmation is up")

        mo.click(*alert_button(m, base, dm, 0))  # Cancel - and it must NOT
        os88marty.settle(m)                      # launch
        if alert_up(m, base, dm):
            fail("Cancel left the alert up")
        st = m.read(base + dm["dos_state"], 1)[0]
        if st != 1:                             # DST_READY
            fail("[dos_state] is %d after CANCELLING: the launch went ahead "
                 "anyway, which is worse than never asking" % st)
        if "DOS" not in ui.titles() or "Disk" not in ui.titles():
            fail("the desktop did not survive a cancelled launch: %r"
                 % (ui.titles(),))
        print("kdhand: Cancel was honoured and the desktop is untouched")

        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if not alert_up(m, base, dm):
            fail("the second Run did not ask - the confirmation is once per "
                 "LAUNCH and not once per session (SPEC.md 96.42)")
        mo.click(*alert_button(m, base, dm, 1))     # Proceed
        rs = wait_text(m, "READY", secs=150, what="the run under kern_dos")
        kd_kb = topmem(rs, "the run under kern_dos")
        print("kdhand: under kern_dos, the program has %d KB above its PSP"
              % kd_kb)
        for want in ("os8088 DOS gate", "DOS version 3.30",
                     "refused with CF, as it should be"):
            if not any(want in r for r in rs):
                fail("%r is not on the screen under kern_dos: the DOS core is "
                     "not servicing INT 21h over the new back end" % want)

        # **THE WHOLE POINT, IN ONE COMPARISON.** It is the same program, the
        # same disk and the same DOS core; the only difference is that the
        # second run has no operating system under it.
        if kd_kb <= win_kb:
            fail("the program got %d KB under kern_dos against %d KB in the "
                 "window - the handoff gave it NO MORE MEMORY, which is the "
                 "only reason the arm exists (docs/plans/KERN-DOS-PLAN.md 1)"
                 % (kd_kb, win_kb))
        print("kdhand: %d KB against %d - the handoff is worth %d KB"
              % (kd_kb, win_kb, kd_kb - win_kb))

        # --- 4. the exit code, and the restart at the end (§9) ---------------
        m.type_text("x")
        rs = wait_text(m, "exited", secs=90, what="the exit")
        if not any("042" in r or "42" in r for r in rs if "exited" in r):
            fail("the exit line does not carry the program's code 42: %r"
                 % [r for r in rs if "exited" in r])
        print("kdhand: %s" % next(r.strip() for r in rs if "exited" in r))

        # **AND THE MACHINE COMES BACK** (§9). It comes back to a BARE
        # desktop and not to the one we left - `int 19h` is a cold start of
        # os8088, so there are no windows to look for. `ui.up()` is the
        # question that has an answer: `desk_rows` and `menu_nbar` are zero
        # until the boot reaches the desktop, so a machine that never restarts
        # and one that hangs in the loader both read the same as a black
        # screen and this tells them apart from a finished boot.
        m.type_text("x")                    # "Press any key to restart"
        try:
            wait_desktop(m, ui)
        except Exception as e:
            fail("the machine never came back to a desktop after the "
                 "program's `int 19h` (SPEC.md 96.40, §9): %s" % e)
        w = ui.open_drive("A")
        if not w:
            fail("the restarted machine will not open a Disk window - it "
                 "reached a desktop and the disk layer did not come up")
        print("kdhand: the machine restarted into os8088 - titles %r"
              % (ui.titles(),))

    print("kdhand: ok - a DOS program ran with the whole machine and gave it back")


if __name__ == "__main__":
    main()
