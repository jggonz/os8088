#!/usr/bin/env python3
"""kern_mega: a byte stranded in the 8042 by a LOST EDGE is taken out by the
tick's watchdog, so moving the mouse while the live resume runs no longer
kills the keyboard and the mouse (docs/plans/KERN-MEGA-PLAN.md 14.63, 14.64).

    python3 tests/megapiclose.py             the row: both legs
    python3 tests/megapiclose.py --trips N   ...N round trips a leg
    python3 tests/megapiclose.py --leg red   ...one leg (red / green)
    python3 tests/megapiclose.py --keep      ...and leave the E9 logs

The owner's Victory 286, at 343124b2: moving the mouse WHILE THE LIVE RESUME
IS HAPPENING - the return from a DOS program - and the desktop came back with
the mouse AND the keyboard dead (NumLock did not light; only a reset got out).
On a 286 every BIOS call kern_mega reflects is a CPU reset, and the Victory's
BIOS is inferred to initialise the 8259 on that path: ICW1 erases every
pending edge, and a byte the 8042 holds then is never delivered - its one
output buffer full, both devices silent for good. 14.62's drains run at fixed
moments; a hand on the mouse keeps bytes coming after them.

No emulator here loses an edge by itself, so the row builds two private trees
with the PICLOSE knob (Makefile): both 8259s initialised, masks put back, on
the way down of EVERY reflection, on both CPU paths - which Bochs, a 386,
takes too. Each leg boots build/<tree>/mega.img under Bochs with an
IntelliMouse and B: holding KBTRIP.COM (tests/dostrap/kbtrip.asm), and TRIPS
times: starts KBTRIP from the DOS window, sends it a key, and MOVES THE
POINTER from that key until a few seconds after `kern_mega: unparked` - the
whole return. Then, per trip, it asserts

  1. the pointer MOVES on the desktop (mouse_x/mouse_y across a walk), and
  2. a key reaches the desktop - the next trip's command is TYPED into the
     DOS window, so a trip that never starts is a dead keyboard (and after
     the last trip one more command is typed for the same reason).

  RED   PICLOSE=2, the knob with mega_kwd compiled OUT: the 343124b2 kernel
        plus a BIOS that loses edges. It must DIE within TRIPS trips (the
        pointer frozen or the command never reaching DOS) - otherwise the
        knob stranded nothing and the green leg tests nothing.
  GREEN PICLOSE=1, the watchdog in: every trip passes, and E9 says
        `kern_mega: 8042 edge lost` at least once, so the watchdog DID take
        a stranded byte rather than the trips merely being lucky.

Each debugger break releases Bochs's mouse grab, so every probe is followed by
a re-capture. It writes only under build/trees/ and build/bochs-megapiclose*/.
"""
import contextlib
import io
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402
import os88build  # noqa: E402

TRIPS = (int(sys.argv[sys.argv.index("--trips") + 1]) if "--trips" in sys.argv
         else 6)
LEGS = ((sys.argv[sys.argv.index("--leg") + 1],) if "--leg" in sys.argv
        else ("red", "green"))
KNOB = {"red": "PICLOSE=2", "green": "PICLOSE=1"}
LOST = "kern_mega: 8042 edge lost"


def lin(mapf, name):
    with open(mapf) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[2] == name:
                return 0x600 + int(p[1], 16)    # KERNEL_SEG, by identity
    raise SystemExit("megapiclose: no %s in %s" % (name, mapf))


class Leg(object):
    def __init__(self, box, mapf):
        self.box = box
        self.mapf = mapf

    def text(self):
        with open(self.box.e9, "rb") as f:
            return f.read().decode("latin-1")

    def counts(self, *keys):
        t = self.text()
        return [t.count(k) for k in keys]

    def wait(self, keys, before, secs):
        end = time.time() + secs
        while time.time() < end:
            t = self.text()
            for k, n in zip(keys, before):
                if t.count(k) > n:
                    return k
            time.sleep(0.5)
        return None

    def pointer(self):
        mx = lin(self.mapf, "mouse_x")
        n = os.path.getsize(self.box.e9)
        with contextlib.redirect_stdout(io.StringIO()):
            ob.dbg(self.box.out, ["xp /2hx 0x%x" % mx])
        with open(self.box.e9, "rb") as f:
            f.seek(n)
            t = f.read().decode("latin-1")
        self.box.capture()
        m = re.search(r"0x0*%x <bogus\+\s*\d+>:\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)"
                      % mx, t)
        return (int(m.group(1), 16), int(m.group(2), 16)) if m else None

    def command(self, n_ready):
        """KBTRIP typed into the DOS window: True when it STARTED."""
        ob.xd("key", "--delay", 30, *(["BackSpace"] * 12))
        ob.xd("type", "--delay", 120, "KBTRIP")
        ob.xd("key", "Return")
        return self.wait(["KBTRIP ready"], [n_ready], 90) is not None


def wiggle(stop):
    while not stop[0]:
        ob.rel(6, 0)
        ob.rel(-6, 0)


def leg(name, keep):
    t = os88build.tree(KNOB[name], targets=("mega.img", "megarun.o88",
                                            "mbr.bin"))
    out = os.path.join(ROOT, "build", "bochs-megapiclose-" + name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    com = os.path.join(out, "KBTRIP.COM")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com,
                    os.path.join(HERE, "dostrap", "kbtrip.asm")], check=True)
    disk = os.path.join(out, "kbtrip.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", com],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = ob.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    lg = Leg(box, os.path.join(t.dir, "megak", "kernel.map"))
    dead = None
    done = 0
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            return None, 0, 0, "kern_mega never reached protected mode"
        time.sleep(25)
        box.capture()
        ob.dbl(591, 105)                # B:
        time.sleep(10)
        for i in range(1, TRIPS + 1):
            before = lg.counts("KBTRIP ready", "KBTRIP key", "unparked")
            if i == 1:
                ob.goto(165, 123)       # the Disk window's one row
                ob.xd("click", "--repeat", 2, "--delay", 120, 1)
                ok = lg.wait(["KBTRIP ready"], before[:1], 90) is not None
            else:
                ok = lg.command(before[0])
            if not ok:
                dead = ("trip %d: KBTRIP never started - the desktop's "
                        "keyboard did not reach the DOS window" % i)
                ob.shot(os.path.join(out, "dead%02d.png" % i))
                break
            time.sleep(1.5)
            stop = [False]
            th = threading.Thread(target=wiggle, args=(stop,))
            ob.xd("key", "k")
            th.start()                  # ...the pointer moving from the key
            back = lg.wait(["unparked"], before[2:], 90)
            time.sleep(4)               # ...through the whole return
            stop[0] = True
            th.join()
            if not back:
                return None, done, 0, "trip %d: the session never came " \
                    "back" % i
            time.sleep(4)
            a = lg.pointer()
            ob.goto(200 + 40 * (i % 3), 300)
            b = lg.pointer()
            moved = a is not None and b is not None and a != b
            print("megapiclose: %-5s trip %2d  pointer %s -> %s%s  (%d lost "
                  "edges so far)" % (name, i, a, b, "" if moved else "  DEAD",
                                     lg.text().count(LOST)), flush=True)
            if not moved:
                dead = ("trip %d: the pointer did not move after the return "
                        "(%s -> %s)" % (i, a, b))
                ob.shot(os.path.join(out, "mouse%02d.png" % i))
                # ...and the KEYBOARD, which a stranded byte silences with it:
                # said rather than asserted, the pointer having failed already
                kb = lg.command(lg.counts("KBTRIP ready")[0])
                dead += (", and the keyboard %s" %
                         ("still reached the DOS window" if kb else
                          "was dead too"))
                break
            done = i
        else:
            if not lg.command(lg.counts("KBTRIP ready")[0]):
                dead = ("after the last trip: the desktop's keyboard did "
                        "not reach the DOS window")
            else:
                ob.xd("key", "k")
                time.sleep(3)
    finally:
        box.stop()
    hits = lg.text().count(LOST)
    print("megapiclose: %-5s %s  %d of %d trips, %d lost edges re-delivered, "
          "%.0fs under Bochs" % (name, KNOB[name], done, TRIPS, hits,
                                  time.time() - t0), flush=True)
    if not keep and dead is None:
        shutil.rmtree(out, ignore_errors=True)
    return dead, done, hits, None


def main():
    keep = "--keep" in sys.argv
    ob.BOCHSRC = ob.BOCHSRC.replace("type=ps2", "type=imps2")
    fails = []
    for name in LEGS:
        dead, done, hits, err = leg(name, keep)
        if err:
            fails.append("%s: %s" % (name, err))
        elif name == "red":
            if dead is None:
                fails.append("red: PICLOSE=2 survived %d trips - the knob "
                             "stranded nothing, so the green leg proves "
                             "nothing" % TRIPS)
            else:
                print("megapiclose: red   died as it must: " + dead)
        else:
            if dead is not None:
                fails.append("green: " + dead)
            elif hits == 0:
                fails.append("green: every trip passed but E9 has no `%s` - "
                             "nothing was stranded, so the watchdog was not "
                             "exercised" % LOST)
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
