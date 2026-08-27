#!/usr/bin/env python3
"""The machine survives its FIRST DISK ACCESS AFTER THE DESKTOP (SPEC.md
2.9.5.1).

    make && python3 tests/postboot.py

**Every boot gate in this tree stops at the first desktop frame.** `bootsmoke`
asserts the desktop appears, `bootstatus` asserts the status line was composed
and drawn, `splashbar` asserts the bar advanced, `heapmap` reads the claim map
and quits. None of them reads a sector afterwards - so a kernel in which the
first post-boot `int 13h` jumps into `cold_entry`'s padding passed all of them,
and it took a field screenshot of a boot marker stopping at block 31 to find
it.

That is exactly what SPEC.md 2.9.5.1 was: `SPLCALL x` writes the OFFSET half of
a far pointer and reads the segment from the word beside it, so retiring the
pair after `spl_finish` lasted until the next call site - and `disk.inc`'s
per-sector `SPLCALL splf_step` is on the disk path for the life of the machine.

WHAT IT ASSERTS. Boot to a desktop, open drive B: - which mounts the volume,
reads its BPB, FAT and directory, and therefore runs `dsk_xfer`'s notch loop
some hundreds of times - and then require that

  1. a window actually opened (the read completed and the file manager drew),
  2. `[ticks]` is still advancing (the machine did not wedge), and
  3. the desktop chrome is still on the glass (it did not reboot into a
     half-drawn screen either).

Any one of the three alone is weak: a machine that jumped into `cold_entry`
executes `boot_cylrun` as code and can plausibly hang, reset, or come back
having drawn nothing. Three cheap tests over one action cover the ways it
looked in practice.

It is MartyPC's, and on the 360KB pair, because that is the geometry the field
runs and the one whose mount is the most work.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMG = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")


def boot(m):
    """Run to the desktop, polling the one bit that says so.

    `settle` wants the screen STILL, and on a loaded container a 360KB boot can
    outrun its patience while the machine is perfectly healthy - so the wait is
    on `[spl_live]` instead. It has to be a FREE-RUNNING wait and not
    `advance(frames=n)` in bursts: the mouse below is a SERIAL one, and a guest
    stepped in bursts never sees a coherent packet - the cursor simply does not
    move, which reads as the click being lost rather than as the machine never
    having run.
    """
    lin_live = os88sym.linear("spl_live")
    lin_entry = os88sym.linear("cold_entry")
    m.run()
    started, t0 = False, time.time()
    while time.time() - t0 < 300:
        live = m.read(lin_live, 1)[0]
        if not started:
            if live == 1 and m.read(lin_entry, 1)[0] == 0xE9:
                started = True
        elif live == 0:
            time.sleep(3.0)             # ...and the first paint after it
            return
        time.sleep(0.2)
    raise SystemExit("postboot: never reached a desktop - this machine did not "
                     "boot, so nothing below would mean what it says")


def main():
    S = os88sym.linear      # dispcp resolves symbols through a callable
    lin_ticks = os88sym.linear("ticks")
    fail = []
    with os88marty.launch(IMG, apps=APPS, machine=MACHINE, boot=False) as m:
        boot(m)
        before = os88marty._Screen(m)
        print("  desktop: %s" % before)
        if before.field < 0.9:
            raise SystemExit("postboot: the desktop never drew (%s)" % before)

        t0 = int.from_bytes(m.read(lin_ticks, 2), "little")
        wins0 = len(dispcp.win_list(m, S))
        mo = os88mouse.Mouse(marty=m)
        try:
            dispcp.open_drive(m, mo, S, os88marty.settle)
        except Exception as e:                  # a wedged guest fails in here
            fail.append("opening drive B: raised %s" % str(e)[:150])
        time.sleep(3.0)

        t1 = int.from_bytes(m.read(lin_ticks, 2), "little")
        try:
            wins1 = len(dispcp.win_list(m, S))
        except Exception as e:
            wins1 = -1
            fail.append("the window list is unreadable after the mount: %s"
                        % str(e)[:120])
        after = os88marty._Screen(m)
        print("  after the mount: %s ; windows %d -> %d ; ticks %d -> %d"
              % (after, wins0, wins1, t0, t1))

        if t1 == t0:
            fail.append("[ticks] did not advance across the mount: the machine "
                        "is wedged. SPEC.md 2.9.5.1 - a SPLCALL after the blob "
                        "was given back far-calls KERNEL_SEG:3, which is "
                        "cold_entry's padding")
        if wins1 <= wins0:
            fail.append("no window opened for drive B: (%d -> %d): the mount "
                        "did not complete" % (wins0, wins1))
        if after.field < 0.9:
            fail.append("the menu bar is gone after the mount (%s): the "
                        "machine reset or repainted into nothing" % after)

    for f in fail:
        print("FAIL: %s" % f)
    print("postboot: %s" % ("FAILED" if fail else
                            "the first disk access after the desktop is "
                            "survivable"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
