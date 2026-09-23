#!/usr/bin/env python3
"""WHERE A LAUNCHED PROGRAM STANDS, under BOTH arms of one machine (96.44.10).

    make kdostest && python3 tests/kdcwd.py

A program launched out of a subdirectory stands IN it, and everything it opens
by a bare name resolves there, and the path the environment hands it says where
it came from.  Four facts; the windowed box has always got all four right, and
`kern_dos` got the fourth wrong from the day it existed.

**THE POINT IS THE PAIR.**  The DOS core is ONE object joined to two back ends
(96.44), so a row that runs either one alone cannot see a disagreement between
them - and the disagreement is the whole class of defect the split introduces.
This boots one machine, runs CWDHERE.COM windowed, then runs the SAME program
on the SAME disk with the whole machine under it, and requires the two answers
to be identical.

WHAT IT CAUGHT.  `OSAPI_FILE_PATH` is an X cell, so `api_x` puts the caller's
DS in ES and the core's callers pass a bare DS offset; `kern_dos` bound the
door with a far call straight at `dsk_path_x`, which writes to ES:DI and never
reloads ES.  The environment's program path came out as `B:` - the drive and
nothing after it - so a program that builds its data path off its own path
looked in the volume root.  Prince of Persia is the field report: under the
whole-machine arm it answered *"Unable to find necessary files. Please start
program from the default drive and directory."*

VERIFIED TO FAIL: taking the `push es`/`pop es` back out of `dos_k_path` in
kerndos/kdback.inc takes step 2 red with MYPATH `B:` against the window's
`B:\\SUB\\CWDHERE.COM`.  The other three rows stay green, which is why they are
printed too - a row that only checked the CWD would have called this fixed.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import subprocess                                               # noqa: E402
import tempfile                                                 # noqa: E402

import dosmap                                                  # noqa: E402
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402
from kdhand import rec, RD_SEL, RD_PITCH, wait_text            # noqa: E402
from os88geom import KD_SEG                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE_SEG = 0x0060                    # kern_dos's core, where DOS_CBASE sits
MACH = "os8088_5150_herc_sb_720_gla"
# **THE SHIPPED 720KB SYSTEM DISK**, not a gate disk: §96.40.3 pointed
# $(SYSROOT) at the parted package, so every disk a user holds carries
# kern_dos and a separate `kdos720.img` built byte-identical to this one -
# two names for one artefact, which is a false green rather than a test.
SYS = "build/os8088-720.img"
APPS = "build/cwdsub.img"
PROG = "B:/SUB/CWDHERE.COM"
WHOLE = 1                            # the Memory page's second arm
# os88ui.inc's alert geometry, mirrored for tests/kdhand.py's reason.
A_BW, A_BG, A_BH, A_BTNY, TITLE_H = 72, 12, 13, 46, 18

WANT = {
    "DRIVE": "B:",
    "DIR": "\\SUB",
    "BARE": "opened HERE.TXT beside me",
    "MYPATH": "B:\\SUB\\CWDHERE.COM",
}


def fail(msg):
    print("kdcwd: FAIL: %s" % msg)
    sys.exit(1)


def answers(rows, arm):
    """The probe's four lines, as {key: value}."""
    got = {}
    for r in rows:
        for k in WANT:
            if r.strip().startswith(k + " "):
                got[k] = r.strip()[len(k):].strip()
    miss = [k for k in WANT if k not in got]
    if miss:
        fail("%s: the probe printed no %s line. What was on the screen:\n%s"
             % (arm, "/".join(miss), "\n".join("   | " + r.rstrip()
                                               for r in rows if r.strip())))
    return got


RAHWANT = ("dsk_rah_seg", "dsk_rah_runs", "kd_top", "kd_spent")


def kdsyms(want):
    """{name: offset} for the SHIPPED kern_dos, proved to be its own.

    kdbigexe.py's and kdarena.py's: the Makefile emits no map for
    build/kerndos.bin, so this assembles the same root again and compares the
    BINARY before an offset is trusted - a map of another build resolves every
    name to a plausible wrong address.
    """
    kdbin = os.path.join(ROOT, os88build.at("build/kerndos.bin"))
    if not os.path.exists(kdbin):
        fail("build/kerndos.bin is not built - `make kdostest`")
    with tempfile.TemporaryDirectory() as td:
        binp = os.path.join(td, "k.bin")
        mapp = os.path.join(td, "k.map")
        root = os.path.join(td, "root.asm")
        with open(root, "w") as f:
            f.write("[map all %s]\n%%include \"%s\"\n"
                    % (mapp, os.path.join(ROOT, "kerndos", "kdos.asm")))
        cmd = ["nasm", "-f", "bin", "-w+error", "-DDOS_EXTCORE"]
        for inc in ("kernel", "kerndos", "apps", "apps/dos", "drivers/net"):
            cmd += ["-I", os.path.join(ROOT, inc) + os.sep]
        cmd += ["-o", binp, root]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            fail("kern_dos would not assemble for its map:\n%s" % r.stderr)
        if open(binp, "rb").read() != open(kdbin, "rb").read():
            fail("the map describes a DIFFERENT kern_dos to the one on the "
                 "disk. Rebuild (`make kdostest`) before trusting this")
        out = {}
        for ln in open(mapp):
            p = ln.split()
            if len(p) == 2 and p[1] in want:
                out[p[1]] = int(p[0], 16)
            elif len(p) == 3 and p[2] in want:
                out[p[2]] = int(p[1], 16)
        missing = [w for w in want if w not in out]
        if missing:
            fail("nasm's map has no %s" % ", ".join(missing))
        return out


def rah_check(m, dm):
    """THE ARENA IS NOT THE ALLOCATOR'S ANY MORE (SPEC.md 96.44.11.3).

    `kern_dos` claims downward from `[kd_top]` and `kd_arena` carves the
    program's block and the file window off that same word, so after
    `kd_giveback` there is no free memory left at the ceiling at all. A mount
    is what breaks it: `dsk_rah_want` runs from `disk_mount`, refuses only
    when `[dsk_rah_seg]` is non-zero, and the ladder had just set it to ZERO -
    so the first file the program opens re-claims 32 KB straight through the
    running program and its window, and the window's next refill is
    overwritten between the read and the copy.

    CWDHERE.COM has opened HERE.TXT by the time this runs, which is the mount.

    **TWO CHECKS, AND THE FIRST IS THE ONE THAT BITES HERE.** Commenting the
    `[kd_spent]` store out of `kd_giveback` and running this takes it red on
    the LATCH and not on the overlap: CWDHERE is a 462-byte program that opens
    one file and stops, so the re-claim has not happened yet when the row
    reads the cells. The overlap check is the net for the shape the field
    reported - Prince of Persia opens a file every few hundred calls for three
    minutes, and there the cache is back at 9800..9FE0 against a window at
    9E00..A000 - and it is kept because the latch is the FIX while the overlap
    is the DEFECT, and a later design that closes the allocator some other way
    should still be held to the second.
    """
    syms = kdsyms(RAHWANT)

    def w(n):
        b = m.read((KD_SEG << 4) + syms[n], 2)
        return b[0] | (b[1] << 8)

    def core(n):
        b = m.read((CORE_SEG << 4) + dm[n], 2)
        return b[0] | (b[1] << 8)

    seg, runs, top = w("dsk_rah_seg"), w("dsk_rah_runs"), w("kd_top")
    spent = w("kd_spent") & 0xFF
    wseg, wbytes = core("dos_wseg"), core("dos_wbytes")
    arena = core("dos_arena")
    print("kdcwd: kern_dos rah_seg=%04X runs=%d kd_top=%04X spent=%d "
          "arena=%04X wseg=%04X wbytes=%04X"
          % (seg, runs, top, spent, arena, wseg, wbytes))
    if not spent:
        fail("[kd_spent] is 0 after the handover: kd_giveback did not close "
             "the allocator, so the next mount will re-claim the read-ahead "
             "out of the program's own memory (SPEC.md 96.44.11.3)")
    if seg == 0:
        return
    lo, hi = seg, seg + ((runs * 9 * 512 + 15) >> 4)
    plo, phi = arena, wseg + (wbytes >> 4)
    if lo < phi and plo < hi:
        fail("the read-ahead cache is INSIDE the program: %04X..%04X against "
             "the program and window at %04X..%04X. A claim after the "
             "handover does not take free memory, it takes theirs "
             "(SPEC.md 96.44.11.3)" % (lo, hi, plo, phi))


def main():
    with os88ui.boot(SYS, apps=APPS, machine=MACH) as ui:
        m = ui.m
        ui.path(PROG)                # the association RUNS it, windowed
        dm = dosmap.package()
        mo = os88mouse.Mouse(marty=m)

        # --- 1: the WINDOW's own DOS ----------------------------------------
        win = answers(wait_text(m, "READY", secs=180,
                                what="the windowed run"), "windowed")
        for k in ("DRIVE", "DIR", "BARE", "MYPATH"):
            print("kdcwd: windowed %-7s %s" % (k, win[k]))
        for k, v in WANT.items():
            if win[k] != v:
                fail("windowed: %s is %r and should be %r - the WINDOW's own "
                     "answer is wrong, so this is not a seam defect at all "
                     "(SPEC.md 96.6.1)" % (k, win[k], v))
        m.type_text("x")
        os88marty.settle(m)

        # **RE-READ THE INSTANCE**: the windowed run claimed the DOS arena, so
        # the segment read before it is stale and every rect below would come
        # back in the tens of thousands.
        pseg = dosmap.instance(m)

        # --- 2: ...and the same program with the whole machine under it -----
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = rec(m, pseg, dm, RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        sel = rec(m, pseg, dm, RD_SEL)
        if sel != WHOLE:
            fail("clicking the Shut down the OS arm left OS88UI_RD_SEL at %d, so the run "
                 "below would be the WINDOWED one again and the comparison "
                 "would be of a thing with itself" % sel)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)

        # the handover's confirmation, found through the WM rather than through
        # the package: the launch moves the instance under us.
        t0 = time.time()
        while time.time() - t0 < 90:
            f = ui.front()
            if f is not None and f.visible and f.w <= 400 and f.h <= 200:
                row = 2 * (A_BW + A_BG) - A_BG
                left = f.x + (f.w - row) // 2 + (A_BW + A_BG)
                mo.click(left + A_BW // 2,
                         f.y + TITLE_H + A_BTNY + A_BH // 2)
                break
            time.sleep(1.0)
        else:
            fail("the handover was never asked for - `Open windows are lost. "
                 "Proceed?` is once per LAUNCH (SPEC.md 96.42) and without it "
                 "the run below is not under kern_dos")

        kd = answers(wait_text(m, "READY", secs=240,
                               what="the run under kern_dos"), "kern_dos")
        for k in ("DRIVE", "DIR", "BARE", "MYPATH"):
            print("kdcwd: kern_dos %-7s %s" % (k, kd[k]))

        # ...and the memory the program was handed is still ITS OWN
        rah_check(m, dm)

        # --- 3: the two arms are the same program on the same disk ----------
        bad = [k for k in WANT if kd[k] != win[k]]
        if bad:
            fail("the two arms disagree about %s: the window says %s and "
                 "kern_dos says %s. The DOS core is ONE object joined to two "
                 "back ends (SPEC.md 96.44), so every one of these is a fact "
                 "about the program and not about which arm ran it"
                 % (", ".join(bad),
                    " / ".join("%s=%r" % (k, win[k]) for k in bad),
                    " / ".join("%s=%r" % (k, kd[k]) for k in bad)))
        print("kdcwd: both arms agree on all four - drive, directory, a "
              "bare-name open and the environment's own path")


if __name__ == "__main__":
    main()
