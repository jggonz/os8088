#!/usr/bin/env python3
"""The DOS file-handle gate (SPEC.md 96.11, docs/plans/DOS-EXEC-PLAN.md 6.3).

os8088 has no file handle anywhere - the whole published API is by name and
by whole file - so the handle layer is built in the package out of those
pieces, over ONE cluster-aligned window carved off the top of the arena. This
row is what says the bytes survive that.

WHAT IT WOULD CATCH:

  - a window that refills at the wrong base    -> a WRONG byte at a window
    (the read path's whole reason to exist)       seam, named with its offset
  - the first flush APPENDING instead of        -> "FAILED at write", because
    replacing (OSAPI_FILE_APPEND refuses a         a file that does not exist
    file whose size is not a cluster multiple)     cannot be appended to
  - a partial window flushed mid-file           -> the next append refuses,
    (which makes the size stop being a             for the same reason
    cluster multiple)
  - the position and the size drifting apart    -> "FAILED at write": the
    across a multi-window write                    append-only check refuses
  - AH=42h not being applied before the read    -> a wrong byte at 12345

It runs on MartyPC and must: it writes a real file through a real FAT12
driver on a real 8088, which is what the layer is over.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
FIL = "build/dosfile360.img"

NBYTES = 20480


def fail(msg):
    print("dosfile: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, FIL):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=FIL) as ui:
        m = ui.m
        if not ui.path("B:/DOSFILE.COM"):
            fail("double-clicking DOSFILE.COM opened no window")

        # Writing 20KB through a 1KB-cluster floppy is real disk work on a
        # 4.77MHz 8088, so the wait is generous and the FAILED lines below
        # are what ends it early.
        rows = []
        end = time.time() + 240.0
        while time.time() < end:
            rows = m.screen() or []
            text = "\n".join(r.rstrip() for r in rows)
            if "READY" in text:
                break
            time.sleep(0.5)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosfile: the bracket's text screen:")
        for r in rows:
            if r.strip():
                print("   | %s" % r.rstrip())

        for line in (l.strip() for l in rows):
            if line.startswith("FAILED"):
                fail("the program reported: %s" % line)

        for want in ("WROTE 20480 and closed",
                     "SIZE %d" % NBYTES,
                     "READ %d" % NBYTES,
                     "SEEK ok",
                     "INPLACE ok",      # the seek-back rewrite (SPEC.md 96.11.6)
                     "GREW ok",         # ...and a write at the END of it
                     "CROSS ok",        # ...across the slack/append boundary
                     "GAP ok",          # a seek PAST the end (SPEC.md 96.11.6.1)
                     "ZLEN ok",         # ...and CX=0 there (SPEC.md 96.11.6.2)
                     "SHRINK ok",       # ...and CX=0 SHORT of the end, all
                                        # three arms of the rewrite
                     "GONE ok"):
            if want not in text:
                fail("%r is not on the screen - the handle layer did not "
                     "complete (SPEC.md 96.11)" % want)

        print("dosfile: wrote, read back and verified %d bytes across two "
              "window crossings, seeked, rewrote IN PLACE, grew it past a "
              "SEEK's gap and deleted"
              % NBYTES)

        m.type_text("x")
        os88marty.settle(m)
        titles = ui.titles()
        if "Disk" not in titles:
            fail("the desktop did not come back after the bracket: %r"
                 % (titles,))

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosfile.png", wd, ht, data)
        print("dosfile: build/dosfile.png written")

    print("dosfile: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
