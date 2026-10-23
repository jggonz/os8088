#!/usr/bin/env python3
"""kern_mega: the PS/2 mouse after a hibernate's RESUME (docs/plans/
KERN-MEGA-PLAN.md 14.56, 14.57; SPEC.md 87.6, 9.9.8).

    python3 tests/megap2hiber.py            the row
    python3 tests/megap2hiber.py --keep     ...and leave the boots' output
    python3 tests/megap2hiber.py --build D  a tree of its own (mega.img,
                                            megak/, megarun.o88, mbr.bin):
                                            how the failing cases were run

The owner's Victory 286 came back from a kern_mega hibernate with everything
alive but the PS/2 mouse. This boots build/mega.img's kernel under Bochs off
C:'s boot partition (tools/os88bochs.py --csys --bootc --noa) with Bochs's
mouse an INTELLIMOUSE (`type=imps2`), so the boot's wheel knock succeeds and
hbm_wake's re-knock runs on the resume - the path the field machine took.
It reads the 8259 masks and mou_p2wz, hibernates through the System menu,
powers off, boots C: again, sets a debugger breakpoint at hbm_wake's .nowz
(in the hibernated session's HIBER.DRV, at the linear base its E9 line
printed), resumes, and checks:

  1. at .nowz - after the knock, before the device's F4 - the SLAVE's IMR
     has IRQ12 MASKED. The knock's ACKs are read by polling 60h, and with
     IRQ12 open and IF on mou_p2_isr races the poll for each one; on a real
     8042 either side wins any one byte, and an ACK the ISR takes mid-knock
     leaves the device waiting for a sample-rate ARGUMENT, which the F4 is
     then spent as: reporting stays off and the mouse is dead. Bochs raises
     IRQ12 after the poll has seen OBF, so the race itself never shows here
     and the mask is what is asserted;
  2. `kern_mega: resumed`;
  3. the MASTER's IMR after the resume is the one before the hibernate. Step
     3d's `not [mou_line]` unmask was 0 for a PS/2 pointer (MOU_P2LINE is
     0xFF) and opened the whole master 8259: B8h before, 00h after;
  4. mou_p2wz is still 3 - the wheel came home;
  5. the pointer MOVES: mouse_x/mouse_y after two different X pointer walks
     differ.

THE FAILING CASE (docs/WRITING-TESTS.md 1): take the `or al, MOU_P2SL` mask
out of hbm_wake's PS/2 block and (1) goes red (slave 8Fh at .nowz); take the
MOU_P2LINE guard out and (3) does (master 00h against B8h). Both verified
when the row was written.

Each debugger break releases Bochs's mouse grab (the prompt takes the
pointer), so every probe is followed by a re-capture. It reads build/
(wants=) and writes only under build/bochs-megap2hiber/.
"""
import contextlib
import io
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402

BUILD = os.path.join(ROOT, "build")
TREE = (sys.argv[sys.argv.index("--build") + 1] if "--build" in sys.argv
        else BUILD)
MAP = os.path.join(TREE, "megak", "kernel.map")
OUT = os.path.join(BUILD, "bochs-megap2hiber")
P2SL = 0x10                     # IRQ12's bit at the slave (MOU_P2SL)


def mapsym(name):
    with open(MAP) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[2] == name:
                return int(p[1], 16)
    raise SystemExit("megap2hiber: no %s in %s" % (name, MAP))


def lin(name):
    return 0x600 + mapsym(name)     # KERNEL_SEG's data, by identity


def since(box, n):
    with open(box.e9, "rb") as f:
        f.seek(n)
        return f.read().decode("latin-1")


def pic(text):
    m = re.search(r"master IMR = ([0-9a-f]+)", text)
    s = re.search(r"slave IMR = ([0-9a-f]+)", text)
    return (int(m.group(1), 16) if m else None,
            int(s.group(1), 16) if s else None)


def xp(text, addr, width):
    """The values `xp /N{b,h}x addr` printed."""
    vals, on = [], False
    for line in text.splitlines():
        m = re.match(r"<bochs:\d+> xp /\d+[bh]x (0x[0-9a-f]+)", line)
        if m:
            on = int(m.group(1), 16) == addr
            continue
        m = re.match(r"0x[0-9a-f]+ <bogus\+\s*\d+>:\s+(.*)", line)
        if m and on:
            vals += [int(w, 16) for w in m.group(1).split()]
    return vals


def probe(box):
    """Break in, read the pointer, mou_p2wz and both masks, carry on - and
    take Bochs's grab back, which the prompt released."""
    n = os.path.getsize(box.e9)
    mx, wz = lin("mouse_x"), lin("mou_p2wz")
    with contextlib.redirect_stdout(io.StringIO()):
        ob.dbg(box.out, ["xp /2hx 0x%x" % mx, "xp /1bx 0x%x" % wz,
                         "info device pic"])
    t = since(box, n)
    box.capture()
    xy = xp(t, mx, 2)
    w = xp(t, wz, 1)
    return (tuple(xy) if len(xy) == 2 else None,
            w[0] if w else None, pic(t))


def menu_hibernate():
    ob.goto(10, 5)                  # System menu > Hibernate... (megahiber's)
    ob.xd("mousedown", 1)
    for _ in range(5):
        ob.rel(0, 30)
    ob.rel(0, 16)
    time.sleep(0.5)
    ob.xd("mouseup", 1)
    time.sleep(10)
    ob.goto(225, 225)               # Hibernate
    ob.xd("click", 1)


def main():
    keep = "--keep" in sys.argv
    ob.BOCHSRC = ob.BOCHSRC.replace("type=ps2", "type=imps2")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    t0 = time.time()
    fails = []
    # --- 1. the session, its masks, and the hibernate ----------------------
    box = ob.Box(OUT, [os.path.join(BUILD, "mines.o88")], bootc=True,
                 noa=True, csys=True, build=TREE)
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        time.sleep(20)
        box.capture()
        ob.goto(100, 100)
        _, wz0, (m0, s0) = probe(box)
        print("megap2hiber: before - master %s slave %s wz %s" % (
            m0 is not None and "%02X" % m0, s0 is not None and "%02X" % s0,
            wz0))
        if wz0 != 3:
            fails.append("the boot's knock did not take (mou_p2wz %s, not 3)"
                         " - is Bochs's mouse an IntelliMouse?" % wz0)
        menu_hibernate()
        end = time.time() + 400         # the off screen has no E9 line of
        last, lt = None, time.time()    # its own: the lines stop
        while time.time() < end:
            n = len(box.lines())
            if n != last:
                last, lt = n, time.time()
            elif time.time() - lt > 30:
                break
            time.sleep(2)
        lines = box.lines()
    finally:
        box.stop()
    shutil.copy(box.e9, os.path.join(OUT, "e9-hibernate.out"))
    hib = [l for l in lines if "above 1MB: HIBER.DRV" in l]
    if not hib:
        fails.append("the hibernate never loaded HIBER.DRV above 1MB")
        return report(fails, keep, t0)
    base = int(re.search(r"HIBER\.DRV ([0-9A-Fa-f]{8})", hib[-1]).group(1),
               16)
    bp = base + mapsym("hbm_wake.nowz")
    # --- 2. the resume, stopped at .nowz ------------------------------------
    box = ob.Box(OUT, [], cmode="keep", bootc=True, noa=True, build=TREE)
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            fails.append("the second boot never reached protected mode")
            return report(fails, keep, t0)
        time.sleep(20)
        box.capture()
        with contextlib.redirect_stdout(io.StringIO()):
            ob.dbg(box.out, ["lb 0x%x" % bp])
        box.capture()
        ob.goto(225, 225)               # Resume
        ob.xd("click", 1)
        if not box.wait_for("Breakpoint 1", 180):
            fails.append("hbm_wake's .nowz (linear %X) never reached" % bp)
        else:
            n = os.path.getsize(box.e9)
            with open(os.path.join(box.out, "dbg.fifo"), "w") as fh:
                fh.write("info device pic\nd 1\nc\n")
            time.sleep(3)
            _, s = pic(since(box, n))
            print("megap2hiber: at .nowz - slave %s" % (
                s is not None and "%02X" % s))
            if s is None or not s & P2SL:
                fails.append("IRQ12 OPEN at the slave through the re-knock "
                             "(slave IMR %s at .nowz): mou_p2_isr races the "
                             "poll for every ACK" % (
                                 s is not None and "%02X" % s))
        if not box.wait_for("kern_mega: resumed", 120):
            fails.append("no `kern_mega: resumed`")
        time.sleep(20)
        box.capture()
        ob.goto(100, 100)
        a, wz1, (m1, s1) = probe(box)
        ob.goto(300, 200)
        b, _, _ = probe(box)
        ob.shot(os.path.join(OUT, "resumed.png"))
        print("megap2hiber: after - master %s slave %s wz %s, pointer %s "
              "then %s" % (m1 is not None and "%02X" % m1,
                           s1 is not None and "%02X" % s1, wz1, a, b))
        if m1 != m0:
            fails.append("the master 8259's mask came home %s, not the %s "
                         "it hibernated with (mou_line's unmask)" % (
                             m1 is not None and "%02X" % m1,
                             m0 is not None and "%02X" % m0))
        if wz1 != 3:
            fails.append("the wheel did not come home (mou_p2wz %s)" % wz1)
        if a is None or b is None or a == b:
            fails.append("the PS/2 pointer did not move after the resume "
                         "(%s then %s)" % (a, b))
        for l in box.lines():
            if l.startswith("kern_mega FAULT"):
                fails.append("resume: " + l)
    finally:
        box.stop()
    return report(fails, keep, t0)


def report(fails, keep, t0):
    print("megap2hiber: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megap2hiber: the boots are in " + OUT)
    else:
        shutil.rmtree(OUT, ignore_errors=True)
    if not fails:
        print("megap2hiber: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
