#!/usr/bin/env python3
"""A fork snapshot's holders end with the emulator that made them.

    python3 tests/martyfork.py

`os88marty.Forks` (docs/plans/completed/SNAPSHOT-PLAN.md B) forks a HOLDER
process per snapshot that blocks reading a pipe and exits when the pipe reaches
EOF - which it does only when every write end is closed. The holder used to
inherit its own pipe's write end and every earlier snapshot's, so it never
saw its parent go: after one soak fourteen of them were still alive hours
later, idle, holding ~700 MB, each with its run directory already deleted,
and invisible to `os88marty.py reap` because the registry names only the
emulator it launched.

This boots once, restores one snapshot twice, takes a second snapshot it
never restores, closes the emulator, and asserts that no martypc_headless
running in THIS process's instance directories is left. Red on a MartyPC
built without debug_server.rs's close of the holder's write ends (two
survivors), green with it.
"""
import os
import subprocess
import sys
import time

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
import os88marty


def ours():
    """martypc_headless processes whose cwd is one of OUR instance dirs -
    other sessions' emulators on the same box are none of this row's business."""
    mine = "/inst/%d-" % os.getpid()
    out = subprocess.run(["pgrep", "-f", "martypc_headless"],
                         capture_output=True, text=True).stdout.split()
    keep = []
    for p in out:
        try:
            if mine in os.readlink("/proc/%s/cwd" % p):
                keep.append(int(p))
        except OSError:
            pass
    return sorted(keep)


def main():
    m = os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                         machine="os8088_5150_cga_gla", label="martyfork")
    try:
        with os88marty.Forks(m) as fk:
            for _ in range(2):
                with fk.restore() as r:
                    r.run()
                    time.sleep(0.5)
            m.snapshot()                    # held, never restored
            during = ours()
            print("martyfork: %d processes while held: %s" % (len(during), during))
            if len(during) < 3:
                print("martyfork: FAIL: expected the emulator and two holders")
                return 1
    finally:
        m.close()
    left = []
    for _ in range(40):                     # a holder exits on EOF, promptly
        left = ours()
        if not left:
            break
        time.sleep(0.25)
    if left:
        print("martyfork: FAIL: %d holder(s) outlived the emulator: %s"
              % (len(left), left))
        for p in left:                      # ours, by cwd: do not leak them
            try:
                os.kill(p, 9)
            except OSError:
                pass
        return 1
    print("martyfork: ok - every holder ended with its emulator")
    return 0


if __name__ == "__main__":
    sys.exit(main())
