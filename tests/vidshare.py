#!/usr/bin/env python3
"""ONE BOOT FOR SEVERAL ROWS' LEGS (docs/plans/completed/SOAK-SPEED.md 3,
docs/plans/soak-speed-2/vid.md): a family of rows that each built their own
360KB B: disk and booted the SAME machine off the SAME system disk to play
it paid one boot apiece. This boots once, waits the motor off, snapshots
(`os88marty.Forks`), and hands each leg a FRESH restore of that desktop with
the leg's own disk swapped into B: - so every leg starts from the same bytes
a boot of its own would have reached, and only the disk differs.

    python3 tests/vidpage.py --shared NAME="ARGS" NAME="ARGS" ...

Each NAME is the row the leg used to be, ARGS that row's arguments; the
script's `main(argv, boot=)` runs it unchanged with `boot` this module's
`Shared.boot`. Every leg's output is headed with its NAME, a leg that raises
is a FAIL of that leg and the rest still run, and the summary names every
leg that failed.

THE SWAP (SPEC.md 18.9.1): an XT drive has no change line, so os8088 takes a
new medium at the first access after the motor has STOPPED - the snapshot is
taken more than the 2.03 guest seconds after the boot's last access, so the
first B: access in every restore reads the new disk's boot sector. Two
volumes are told apart by their serial (os88disk.py derives it from the
content), so a leg whose disk has the boot disk's serial is refused here
rather than run against the first disk's cached directory (a leg whose
disk is the boot disk byte for byte has nothing to swap, and keeps it).
"""
import contextlib
import os
import shlex
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import os88marty, os88ui                                    # noqa: E402

MOTOR = int(4.772727e6 * 3)             # > the 2.03 s motor-off (fdswap)


def serial(path):
    """the BPB's volume serial: bytes 39..42 of the boot sector"""
    with open(path, "rb") as f:
        return f.read(43)[39:43]


def digest(path):
    import hashlib
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).digest()


class Shared(object):
    """The boot the legs share: the first `boot()` makes it, every `boot()`
    answers a restore of it with that call's `apps` in B:."""

    def __init__(self):
        self.stack = contextlib.ExitStack()
        self.key = self.fk = self.ser = None
        self.legs = 0

    def close(self):
        self.stack.close()

    @contextlib.contextmanager
    def boot(self, image, apps=None, machine=None, quick=False, **kw):
        key = (os.path.realpath(image), machine, quick,
               tuple(sorted(kw.items())))
        first = self.fk is None         # (its disk is the one booted with)
        if first:
            t0 = time.time()
            ui = self.stack.enter_context(os88ui.boot(
                image, apps=apps, machine=machine, quick=quick, **kw))
            # the boot's last floppy access long over: the motor is OFF, so
            # the first B: access after a swap reads the new disk
            ui.m.advance(cycles=MOTOR)
            self.fk = self.stack.enter_context(os88marty.Forks(ui.m))
            self.key, self.ser, self.dig = key, serial(apps), digest(apps)
            print("   [shared boot: %s, %.1f host s]"
                  % (machine, time.time() - t0))
        elif key != self.key:
            raise SystemExit("vidshare: a leg wants %r, the shared boot is "
                             "%r - give it a row of its own" % (key, self.key))
        self.legs += 1
        with self.fk.restore() as r:
            if not first and digest(apps) != self.dig:  # (the same bytes:
                if serial(apps) == self.ser:                # nothing to swap)
                    raise SystemExit("vidshare: this leg's disk has the boot "
                                     "disk's serial, so the swap would go "
                                     "unseen")
                r.mount(1, apps)
            yield os88ui.UI(r, card=kw.get("card"))


def run(main, argv):
    """`--shared NAME=ARGS ...`: every leg through `main(args, boot=)` on one
    boot. Answers the exit status: 1 if any leg failed."""
    legs = []
    for a in argv:
        name, eq, args = a.partition("=")
        if not eq or not name:
            sys.exit("vidshare: a leg is NAME=\"ARGS\", not %r" % a)
        legs.append((name, shlex.split(args)))
    sh, failed = Shared(), []
    try:
        for name, args in legs:
            print("\n=== %s: %s" % (name, " ".join(args) or "(defaults)"))
            sys.stdout.flush()
            t0 = time.time()
            try:
                rc = main(args, boot=sh.boot)
            except SystemExit as e:
                rc = e.code if isinstance(e.code, int) else 1
                if not isinstance(e.code, int):
                    print("   %s" % e.code)
            except Exception:
                traceback.print_exc(file=sys.stdout)
                rc = 1
            print("=== %s: %s (%.1f host s)" % (
                name, "ok" if not rc else "FAIL", time.time() - t0))
            if rc:
                failed.append(name)
    finally:
        sh.close()
    print("\n%d legs on one boot: %s" % (
        len(legs), "FAIL %s" % " ".join(failed) if failed else "all ok"))
    return 1 if failed else 0
