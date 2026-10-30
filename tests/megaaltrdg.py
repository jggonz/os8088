#!/usr/bin/env python3
"""Alt+Enter into RealDMG's full screen on kern_mega, on a real 286
(docs/plans/KERN-MEGA-PLAN.md 14.119).

    python3 tests/megaaltrdg.py                 the row, both legs
    python3 tests/megaaltrdg.py --leg reflect   one leg (reflect / repeat)

The owner's Victory 286: Alt+Enter took RealDMG full screen, it came straight
out again, and the game usually got an Enter. Two kern_mega defects, both
about the keyboard, and both invisible on kern_big, whose AT keyboard path is
the ROM's own int 09h with int 15h AH=4Fh telling the kernel every byte
(SPEC.md 9.7.2):

  REFLECT  a key a REFLECTED BIOS call took never reached SPEC.md 9.7's
           key-state map. A reflection runs the ROM in real mode on the IVT,
           so IRQ1 there is the ROM's int 09h and kbm_isr is not in it - and
           the bracket's own mode set is a reflection, on a 286 a CPU reset
           each way. Let the Alt+Enter go while it runs and the map holds
           Alt and Enter DOWN for good: os88alt_edge's next both-down is a
           "fresh" press, and the game's pad reads the Start key the
           keyboard is no longer holding. The leg stops the CPU at
           fsx_setbios - the mode set, just before its int 10h - lets both
           keys go there, runs on, and asks the map. mega_r15 is the fix:
           the reflection's int 15h AH=4Fh hands the ROM's byte to
           kbd_track.

  REPEAT   mega_k09 (kern_mega's protected-mode int 09h) TYPED Alt+Enter as
           1C00h - which is KEY_ALTENTER itself, where no ROM's AH=00h hands
           the combination over at all - so every typematic repeat of a held
           Alt+Enter reached ui_task as another KEY_ALTENTER (SPEC.md 9.7.1's
           latch is an edge; the typed copy is not) and inside a bracket the
           game's own int 16h drain took it for Enter. The leg goes full
           screen, then holds Alt+Enter to leave with the keyboard's repeats
           (86Box makes none of its own, so the leg sends Enter's make every
           0.1s after 0.5s): the bracket must close and STAY closed.

86Box's vm/at286-port (MR BIOS 286, 16MHz) with build/mega.img, RealDMG's
kern_mega arm and GBPAD.GB (tools/gbours.py, ours) on a scratch B:. The
assertion is kernel bytes: `[fsx_task]` (FFFFh = no bracket) and
`kbd_dnmap`'s bits for Alt (38h) and Enter (1Ch).

RED (docs/WRITING-TESTS.md 1), measured on the kernel before 14.119
(OS88_MEGA_BUILD=<its tree>): REFLECT leaves Alt and Enter down in the map,
REPEAT goes back into full screen on the first repeat after the leave.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import dispapps  # noqa: E402

NO_BRACKET = 0xFFFF
KSC_ALT, KSC_ENTER = 0x38, 0x1C
TRIALS = 3


def pkgdir():
    import os88build
    return os.path.join(os88build.mega_build(), "megak", "apps")


def dn_off(lst):
    """os88alt_dn's offset: the seed's store names it (`mov byte [x], 1`)."""
    for line in open(lst, errors="replace"):
        m = re.search(r"C606\[([0-9A-F]{2})([0-9A-F]{2})\]01\s+<\d+>\s+"
                      r"mov byte \[os88alt_dn\], 1", line)
        if m:
            return int(m.group(2) + m.group(1), 16)
    sys.exit("megaaltrdg: os88alt_dn not in %s" % lst)


class Box:
    def __init__(self, ui, dnoff):
        self.ui, self.m, self.dnoff = ui, ui.m, dnoff
        self.fsx = self.m.sym("fsx_task")
        self.map = self.m.sym("kbd_dnmap")

    def bracket(self):
        return int.from_bytes(self.m.read(self.fsx, 2), "little")

    def keys(self):
        b = self.m.read(self.map, 8)
        return ((b[KSC_ALT >> 3] >> (KSC_ALT & 7)) & 1,
                (b[KSC_ENTER >> 3] >> (KSC_ENTER & 7)) & 1)

    def dn(self):
        seg = dispapps.pkg_seg(self.m, 0)[1]
        return self.m.read(self.m.lin(seg, self.dnoff), 1)[0]

    def say(self):
        a, e = self.keys()
        return "[fsx_task]=%04X map Alt %d Enter %d, os88alt_dn %d" % (
            self.bracket(), a, e, self.dn())

    def until(self, cond, what, guest=10.0):
        try:
            M.until(self.m, lambda _: cond(), what, poll=0.05, guest=guest)
        except M.MartyError:
            return False
        return True

    def leave(self):
        """Out of full screen whatever the map says: Esc, a level read the
        bracket polls (rw_fsdrain), then the keys tapped so the map is clean
        for the next trial."""
        if self.bracket() != NO_BRACKET:
            self.m.key("Escape", down=True, up=False)
            self.until(lambda: self.bracket() == NO_BRACKET, "the leave", 5.0)
            self.m.key("Escape", down=False, up=True)
        for k in ("AltLeft", "Enter"):
            self.m.key(k)
        M.guest_sleep(self.m, 1.0)


def leg_reflect(bx):
    """Alt+Enter let go while the bracket's mode set is in the BIOS."""
    m = bx.m
    fails = []
    for t in range(TRIALS):
        m.key("AltLeft", down=True, up=False)
        M.guest_sleep(m, 0.05)
        m.bp_exec("fsx_setbios")
        m.key("Enter", down=True, up=False)
        hit = bx.until(m.stopped, "fsx_setbios", 10.0)
        m.bp_exec()
        m.key("Enter", down=False, up=True)     # ...while the CPU stands at
        m.key("AltLeft", down=False, up=True)   # the mode set's int 10h
        if not hit:
            fails.append("trial %d: the bracket never reached fsx_setbios "
                         "(%s)" % (t, bx.say()))
            m.run()
            bx.leave()
            continue
        m.run()
        M.guest_sleep(m, 1.5)
        a, e = bx.keys()
        print("  reflect %d: keys let go in the mode set -> %s" % (t, bx.say()))
        if bx.bracket() == NO_BRACKET:
            fails.append("trial %d: the bracket did not stay up after an "
                         "Alt+Enter let go in its mode set (%s)" % (t, bx.say()))
        if a or e:
            fails.append("trial %d: the key-state map still holds %s after "
                         "both keys were let go during the reflected mode set "
                         "- a key the ROM's int 09h took never reached "
                         "kbd_track (KERN-MEGA-PLAN 14.119)"
                         % (t, " and ".join(n for n, v in (("Alt", a), ("Enter", e)) if v)))
        bx.leave()
    return fails


def leg_repeat(bx):
    """A held Alt+Enter, with the keyboard's repeats, leaves and stays out."""
    m = bx.m
    fails = []
    m.key("AltLeft", down=True, up=False)
    M.guest_sleep(m, 0.05)
    m.key("Enter", down=True, up=False)
    M.guest_sleep(m, 0.15)
    m.key("Enter", down=False, up=True)
    m.key("AltLeft", down=False, up=True)
    if not bx.until(lambda: bx.bracket() != NO_BRACKET, "the bracket"):
        return ["Alt+Enter did not take RealDMG full screen (%s)" % bx.say()]
    if not bx.until(lambda: bx.dn() == 0, "the seed spent"):
        return ["the bracket's poll never saw the keys let go (%s)" % bx.say()]
    M.guest_sleep(m, 1.0)
    m.key("AltLeft", down=True, up=False)
    M.guest_sleep(m, 0.05)
    m.key("Enter", down=True, up=False)
    t, nxt, left, back = 0.0, 0.5, None, None
    while t < 1.5:
        M.guest_sleep(m, 0.05)
        t += 0.05
        st = bx.bracket()
        if st == NO_BRACKET and left is None:
            left = t
        if st != NO_BRACKET and left is not None and back is None:
            back = t
        if t >= nxt:
            m.key("Enter", down=True, up=False)     # the typematic make
            nxt += 0.1
    m.key("Enter", down=False, up=True)
    M.guest_sleep(m, 0.05)
    m.key("AltLeft", down=False, up=True)
    for _ in range(10):
        M.guest_sleep(m, 0.1)
        if bx.bracket() != NO_BRACKET and back is None:
            back = -1
    print("  repeat: left at %s s of the hold, back in at %s -> %s" % (
        left, back, bx.say()))
    if left is None:
        fails.append("a held Alt+Enter never left full screen (%s)" % bx.say())
    elif back is not None:
        fails.append("a held Alt+Enter left full screen and the keyboard's "
                     "repeat took it straight back in: a typed Alt+Enter "
                     "reached ui_task as KEY_ALTENTER (KERN-MEGA-PLAN 14.119)")
    bx.leave()
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--leg", choices=("reflect", "repeat"))
    a = ap.parse_args()
    pdir = pkgdir()
    dnoff = dn_off(os.path.join(pdir, "realdmg.lst"))
    rom = subprocess.check_output(
        [sys.executable, os.path.join(ROOT, "tools", "gbours.py"), "gbpad"],
        text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="megaaltrdg-")
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call(
        [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
         "--size", "720", "--folder", "SYSTEM/APPDATA",
         os.path.join(pdir, "realdmg.o88"), rom], stdout=subprocess.DEVNULL)
    fails = []
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                     machine="os8088_xt_vga_720b", quick=True) as ui:
        ui.path("B:/" + os.path.basename(rom))
        bx = Box(ui, dnoff)

        def running(_):
            got = dispapps.pkg_seg(ui.m, 0)
            return bool(got)
        M.until(ui.m, running, "RealDMG's window", poll=0.25, guest=60.0)
        M.guest_sleep(ui.m, 3.0)
        print("megaaltrdg: RealDMG up - %s" % bx.say())
        for name, leg in (("reflect", leg_reflect), ("repeat", leg_repeat)):
            if a.leg and a.leg != name:
                continue
            got = leg(bx)
            for f in got:
                print("FAIL megaaltrdg %s: %s" % (name, f))
            fails += got
    print("megaaltrdg: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
