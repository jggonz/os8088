#!/usr/bin/env python3
"""OSAPI_FILE_PATH: does it work, and WHAT DOES IT COST? (SPEC.md 19.2.4)

The slot exists because `dsk_find` drops the on-disk dot links, so no package
can walk up - three of them each built a descent stack instead.  But that is
not what this row is for.  **This row is about the DISK OPERATIONS**, because
the design's whole claim is that the walk mounts at no level, and a mount is
invisible from inside the guest: a kernel that re-mounted per level would
answer the identical path and look entirely correct.

So the count comes from OUTSIDE, with os88marty's `disk()` - what the floppy
controller was actually asked to do.  Three assertions:

  1  THE PATH IS RIGHT.  `\\ONE\\TWO\\THREE`, from a package the gate disk puts
     three levels deep.  A root-level package would answer `\\` having read
     nothing, which is why the disk is shaped this way.

  2  THE WALK COSTS LESS THAN THE MOVES ALONE WOULD.  Arm B does three
     round trips with OSAPI_FILE_GOTO_QM - the moves a package-side walk of
     this depth would make and NOTHING else, no directory entry read - and
     that is a FLOOR under such a walk rather than an estimate of one.  If the
     whole path, names and all, costs less than that floor, the design's claim
     is made: it is not paying per level what a chdir pays.

  3  THE SECOND CALL IS CHEAPER THAN THE FIRST.  SPEC.md 19.2.3's cached
     directory window should answer the second walk of the same chain from
     memory.  The package makes the call twice and marks the boundary, so this
     is read as two brackets rather than inferred.

WHAT WOULD BREAK IT, checked by construction rather than by hoping: build the
walk on OSAPI_FILE_GOTO_QM per level and assertion 2 inverts, because that is
precisely what arm B measures.  Answer the path from a stale cache and 1
fails.  Drop 19.2.3's window (`make DIRW1=1`) and 3 fails.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
PT = "build/pathtest360.img"
WANT = "\\ONE\\TWO\\THREE"
DEPTH = 3                    # ...levels, which the gate disk fixes
MOUNT_SECS = 12              # a floppy mount, measured (SPEC.md 18.8.2):
                             # BPB 1 + FAT 9 + directory 1 + ASSOC.DAT 1
MOUNT_FLOOR = DEPTH * MOUNT_SECS  # what a per-level walk could not beat

# tests/pathtest/pathtest.asm's result block, at seg*16 + image
R_STAT, R_STAT2, R_DONE, R_MARK, R_MDONE = 0, 1, 2, 3, 4
R_LEN, R_PATH, R_PATH2 = 6, 16, 144


def fail(msg):
    print("pathcost: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, PT):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=PT, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        S = m.sym

        w = ui.path("B:/ONE/TWO/THREE/PATHTEST.O88")
        if not w:
            fail("could not reach the package three folders down")
        ui.raise_window(w)              # ...and give it the keyboard: three
                                        # Disk windows are open behind it and
                                        # a key goes to whichever is in front

        # Where did the instance land? The result block is just past the image.
        base = None
        for slot in range(8):
            wp = os88geom.winptr(m, slot, S)
            seg = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
            if not seg:
                continue
            img = struct.unpack("<H", m.read(seg * 16 + 8, 2))[0]
            ttl = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
            if ttl and m.read(seg * 16 + ttl, 10).startswith(b"Path Test"):
                base = seg * 16 + img           # ...the bss, just past the
                break                           # image, as heapcheck reads
                                                # heapfrag's. Found by TITLE
                                                # rather than by image size,
                                                # which changes with every
                                                # edit to the package
        if base is None:
            fail("the package's window is not in the table - it did not launch")
        print("pathcost: the gate's result block is at linear 0x%X" % base)

        def b(off):
            return m.read(base + off, 1)[0]

        def w(off):
            return struct.unpack("<H", m.read(base + off, 2))[0]

        def s_(off):
            raw = m.read(base + off, 128)
            return raw.split(b"\0")[0].decode("latin1")

        # --- arm A, in TWO brackets: cold, then warm ------------------------
        os88marty.settle(m)
        m.disk(reset=True)
        m.type_text("p")
        end = time.time() + 60.0
        while time.time() < end and not b(R_MARK):
            time.sleep(0.05)
        if not b(R_MARK):
            fail("the first OSAPI_FILE_PATH never returned")
        cold = m.disk(reset=True)               # ...and the second call starts
        end = time.time() + 60.0                # its own bracket here
        while time.time() < end and b(R_DONE) != 0xA5:
            time.sleep(0.05)
        if b(R_DONE) != 0xA5:
            fail("the second OSAPI_FILE_PATH never returned")
        warm = m.disk()

        st = b(R_STAT)
        if st:
            fail("OSAPI_FILE_PATH refused with FERR %d - the slot did not "
                 "answer at all, so nothing below it is measurable" % st)
        got = s_(R_PATH)
        print("pathcost: the slot answered %r (%d bytes)" % (got, w(R_LEN)))
        if got != WANT:
            fail("the path is %r and the package is in %r - a walk that reads "
                 "the wrong parent, or names the wrong child in the right one"
                 % (got, WANT))
        if w(R_LEN) != len(WANT):
            fail("the length says %d for a %d-character path"
                 % (w(R_LEN), len(WANT)))
        if b(R_STAT2) != 0 or s_(R_PATH2) != WANT:
            fail("the SECOND call answered %r / FERR %d - a walk that is not "
                 "repeatable has left state behind it"
                 % (s_(R_PATH2), b(R_STAT2)))

        cr, cs = cold.get("reads", 0), cold.get("read_sectors", 0)
        wr, ws = warm.get("reads", 0), warm.get("read_sectors", 0)
        print("pathcost: FIRST  walk, %d levels   %3d reads, %3d sectors"
              % (DEPTH, cr, cs))
        print("pathcost: SECOND walk, same chain  %3d reads, %3d sectors"
              % (wr, ws))

        # --- 2: it cannot be mounting per level ------------------------------
        if cs > MOUNT_FLOOR:
            fail("the first walk moved %d sectors for %d levels. A mount is "
                 "about %d sectors of its own, so %d levels that each mounted "
                 "could not come in under %d - which is what this bound is "
                 "for. SPEC.md 19.2.4 says the walk stands nowhere and mounts "
                 "at no level; this number says otherwise"
                 % (cs, DEPTH, MOUNT_SECS, DEPTH, MOUNT_FLOOR))
        print("pathcost: ...under the %d-sector bound %d mounts could not "
              "meet, so it is not mounting per level" % (MOUNT_FLOOR, DEPTH))

        # --- 3: the second walk is answered from 19.2.3's window -------------
        if wr >= cr:
            fail("the second walk of the SAME chain cost %d reads against the "
                 "first's %d. SPEC.md 19.2.3's cached directory window should "
                 "answer it from memory, and dsk_up reads through that "
                 "window - so a second walk that costs the same says the "
                 "window is not being consulted (`make DIRW1=1` is that build "
                 "on purpose)" % (wr, cr))
        print("pathcost: ...and the second is cheaper than the first, which is "
              "19.2.3's window answering warm")

        # --- 4: a same-volume chdir really is free ---------------------------
        m.disk(reset=True)
        m.type_text("g")
        end = time.time() + 60.0
        while time.time() < end and b(R_MDONE) == 0:
            time.sleep(0.1)
        if b(R_MDONE) != 0xA5:
            fail("arm B did not finish (marker %02X) - OSAPI_FILE_HERE or "
                 "GOTO_QM refused" % b(R_MDONE))
        moves = m.disk()
        mr, ms = moves.get("reads", 0), moves.get("read_sectors", 0)
        print("pathcost: six same-volume GOTO_QM  %3d reads, %3d sectors" % (mr, ms))
        if mr:
            fail("six chdirs INSIDE ONE VOLUME cost %d reads. SPEC.md 19.2.2 "
                 "says that is 'a WORD, no I/O at all' - a number here means "
                 "the volume is being re-mounted on a move that should touch "
                 "nothing, and every walk in the tree pays it" % mr)
        print("pathcost: ...which is 19.2.2's 'a WORD, no I/O at all', measured")

    print("pathcost: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
