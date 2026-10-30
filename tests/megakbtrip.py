#!/usr/bin/env python3
"""kern_mega: the keyboard INSIDE a DOS program, and the keyboard and mouse
after it, over many round trips with the pointer moving (docs/plans/
KERN-MEGA-PLAN.md 14.61, 14.62).

    python3 tests/megakbtrip.py              the row (TRIPS round trips)
    python3 tests/megakbtrip.py --trips N    ...N of them
    python3 tests/megakbtrip.py --keep       ...and leave build/bochs-megakbtrip
    python3 tests/megakbtrip.py --build D    a tree of its own (mega.img,
                                             megak/, megarun.o88, mbr.bin):
                                             how the failing case was run
    python3 tests/megakbtrip.py --out D      ...writing there instead (two at
                                             once, on two OS88_BOCHS_DISPLAYs)

The owner's Victory 286 ran Test Drive III through DOS a few times,
then came home with the keyboard and the PS/2 mouse both dead - and on a later
launch the keyboard was dead INSIDE the game. Both are one fact about the
8042: it has ONE output buffer for the two devices, and a byte left in it
that nobody reads (an AUXILIARY byte with IRQ12 masked, or a byte whose edge
an 8259 initialisation erased) keeps the controller from sending anything
more from either. On kern_mega every DOS program takes the park arm, and the
handoff masked IRQ12 for the program with the PS/2 mouse still REPORTING,
held only by 0xA7: a packet byte the mouse sent across the launch was
stranded there for the whole program.

This boots build/mega.img under Bochs with an IntelliMouse (`type=imps2`, as
megap2hiber), B: holding only KBTRIP.COM (tests/dostrap/kbtrip.asm, assembled
here), opens B: and starts it, and then TRIPS times, from the DOS window's
prompt: types kbtrip (TYPED says why lower case) and presses Return WITH THE
POINTER MOVING the whole time (the race), waits for the program's `KBTRIP
ready` on E9, sends one key, and asserts

  1. `KBTRIP key 256B` - THE key reached the DOS program, first (KBTRIP
     reports the first key it reads, so a stray one - an AUX byte read as a
     scan code - fails it too; it gives up after
     30 s and says `nokey` with port 64h's status, which reads 31h - OBF,
     AUX - when this is the defect);
  2. `kern_mega: unparked` - the session came back;
  3. the pointer MOVES after the return (mouse_x/mouse_y before and after a
     walk differ);

and the desktop's keyboard is asserted by the next trip itself: its command
is typed into the DOS window, so a trip that never parks is a dead keyboard.

THE FAILING CASE (docs/WRITING-TESTS.md 1): on the tree before 14.62 (the
0xF5 and the drain out of HIBER.DRV's hbm_dosrun) step 1 fails on 6 trips of
12 - `KBTRIP entry 64=31`, `KBTRIP nokey 64=31` - verified when the row was
written, with `--build` on a copy of that tree.

Each debugger break releases Bochs's mouse grab; os88bochs re-captures inside
the next input after one (docs/BOCHS.md). It reads build/ (wants=) and writes only under
build/bochs-megakbtrip/.
"""
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402

BUILD = os.path.join(ROOT, "build")
TREE = (sys.argv[sys.argv.index("--build") + 1] if "--build" in sys.argv
        else BUILD)
TRIPS = (int(sys.argv[sys.argv.index("--trips") + 1]) if "--trips" in sys.argv
         else 10)
OUT = (os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
       if "--out" in sys.argv else os.path.join(BUILD, "bochs-megakbtrip"))


class Trip(object):
    def __init__(self, box):
        self.box = box

    def text(self):
        with open(self.box.e9, "rb") as f:
            return f.read().decode("latin-1")

    def counts(self, *keys):
        t = self.text()
        return [t.count(k) for k in keys]

    def wait(self, keys, before, secs):
        """The first of KEYS whose count passes BEFORE, or None."""
        end = time.time() + secs
        while time.time() < end:
            t = self.text()
            for k, n in zip(keys, before):
                if t.count(k) > n:
                    return k
            time.sleep(0.1)
        return None

    def pointer(self):
        """(mouse_x, mouse_y) - the Box's read re-captures the mouse inside
        the next input, so no capture() toggle follows it any more"""
        try:
            return self.box.mouse()
        except RuntimeError:
            return None


# The command, in LOWER case: two scan codes a letter where a capital is four
# (shift down, key, key up, shift up). Bochs's GUI hands it X events in
# batches when the host is loaded, and its keyboard's internal queue holds 16:
# `KBTRIP` batched whole is 24 and lost its I in a soak-loaded run (`Bad
# command or file name: KBTRP`), where `kbtrip` is 12. DOS does not care.
# It still lost `kb` once (`TRIP`) behind the BackSpaces at a load average of
# 30, so a trip that never HANDED OVER (no new `parked`) types it once more
TYPED = "kbtrip"


def wiggle(stop):
    while not stop[0]:
        ob.rel(6, 0)
        ob.rel(-6, 0)


def main():
    keep = "--keep" in sys.argv
    ob.MOUSE = "imps2"   # an IntelliMouse. BOCHSRC is a template now
    # (`type={mouse}`), so the replace that stood here matched nothing
    # and every Box booted a plain PS/2 mouse: no wheel, mou_p2wz 0
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    com = os.path.join(OUT, "KBTRIP.COM")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com,
                    os.path.join(HERE, "dostrap", "kbtrip.asm")], check=True)
    disk = os.path.join(OUT, "kbtrip.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", com],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = ob.Box(OUT, [], bimg=disk, btype="1_44", build=TREE)
    tr = Trip(box)
    fails, keys, retyped = [], 0, []
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()                        # the desktop, off kernel state
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        for i in range(1, TRIPS + 1):
            before = tr.counts("KBTRIP ready", "KBTRIP key", "KBTRIP nokey",
                               "unparked")
            parked = tr.counts("kern_mega: parked")[0]
            stop = [False]
            th = threading.Thread(target=wiggle, args=(stop,))
            if i == 1:
                box.goto(165, 123)      # the Disk window's one row
                th.start()
                box.dblclick()
            else:
                ob.xd("key", "--delay", 30, *(["BackSpace"] * 4))
                ob.xd("type", "--delay", 120, TYPED)
                th.start()
                time.sleep(0.4)
                ob.xd("key", "Return")
            ready = tr.wait(["KBTRIP ready"], before[:1], 90 if i == 1 else 45)
            if not ready and tr.counts("kern_mega: parked")[0] == parked:
                # never handed over: the command line was not what was typed
                # (TYPED's note - Bochs dropped part of a burst), or on trip
                # 1 the double-click missed the row (the walk lands off it at
                # a load average of 20), so it is done once more. A dead
                # keyboard fails this one too
                retyped.append(i)
                if i == 1:
                    ob.goto(165, 123)
                    box.dblclick()
                else:
                    ob.xd("key", "--delay", 60, *(["BackSpace"] * 8))
                    ob.xd("type", "--delay", 120, TYPED)
                    time.sleep(0.4)
                    ob.xd("key", "Return")
                ready = tr.wait(["KBTRIP ready"], before[:1], 90)
            stop[0] = True
            th.join()
            if not ready:
                fails.append("trip %d: KBTRIP never started - the desktop's "
                             "keyboard did not reach the DOS window" % i)
                ob.shot(os.path.join(OUT, "dead%02d.png" % i))
                break
            time.sleep(1.5)
            ob.xd("key", "k")
            got = tr.wait(["KBTRIP key", "KBTRIP nokey"], before[1:3], 45)
            lines = [l for l in tr.text().splitlines()
                     if l.startswith("KBTRIP")][-3:]
            # THE key, `k` (scan 25h, ASCII 6Bh): a stray one first - an AUX
            # byte read as a scan code, `03 32` on the old tree - is the same
            # defect leaking the other way
            if got == "KBTRIP key" and lines[-1].startswith("KBTRIP key 256B"):
                keys += 1
            else:
                fails.append("trip %d: `k` was not the key the DOS program "
                             "read (%s)" % (i, " / ".join(lines)))
            if not tr.wait(["unparked"], before[3:], 90):
                fails.append("trip %d: the session never came back" % i)
                break
            # the desktop back (was 6 s slept), then the pointer CLOSED-LOOP:
            # a goto that cannot move it raises, and that is the DEAD case
            try:
                box.ui_done("the desktop after the return")
            except RuntimeError as e:
                print("megakbtrip: %s" % e)
            a = tr.pointer()
            try:
                box.goto(200 + 40 * (i % 3), 300)
            except RuntimeError:
                pass
            b = tr.pointer()
            moved = a is not None and b is not None and a != b
            print("megakbtrip: trip %2d  %-5s  %s  pointer %s -> %s%s"
                  % (i, "key" if got == "KBTRIP key" else "NOKEY",
                     " / ".join(lines), a, b, "" if moved else "  DEAD"),
                  flush=True)
            if not moved:
                fails.append("trip %d: the pointer did not move after the "
                             "return (%s -> %s)" % (i, a, b))
                ob.shot(os.path.join(OUT, "mouse%02d.png" % i))
                break
        else:
            # the desktop's keyboard after the last trip: one more command
            n = tr.counts("KBTRIP ready")
            ob.xd("key", "--delay", 30, *(["BackSpace"] * 4))
            ob.xd("type", "--delay", 120, TYPED)
            ob.xd("key", "Return")
            if not tr.wait(["KBTRIP ready"], n, 90):
                fails.append("after the last trip: the desktop's keyboard "
                             "did not reach the DOS window")
            else:
                ob.xd("key", "k")
    finally:
        box.stop()
    print("megakbtrip: %d of %d keys reached the DOS program, %.0fs under "
          "Bochs" % (keys, TRIPS, time.time() - t0))
    if retyped:
        print("megakbtrip: the launch made twice on trip(s) %s - the first "
              "never handed over" % ", ".join(map(str, retyped)))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not keep:
        shutil.rmtree(OUT, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
