#!/usr/bin/env python3
"""The DOS directory, find and vector gate (SPEC.md 96.12).

The three groups that landed beside the file handles and that a DOS program
uses without thinking about them: the interrupt vectors, the DTA and
find-first/next over it, and make/change/ask/remove directory.

THE FIND COUNTS ARE THE ROW. The gate disk carries A.TXT, BB.TXT, CCC.TXT,
DATA.DAT and the program, chosen so that the three patterns give three
DIFFERENT numbers - `*.*` five, `*.TXT` three, `?.TXT` one. A matcher that
ignores wildcards, one that matches the printable NAME.EXT form instead of
the 8.3 one, and one that lets `*` run past the dot each get a different
number wrong, and no two of them agree.

AH=47h is checked against a name this program CHOSE - it makes SUBDIR, stands
in it, and asks - so the '..' walk cannot pass by answering something already
on the disk.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
DIR = "build/dosdir360.img"


def fail(msg):
    print("dosdir: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, DIR):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=DIR) as ui:
        m = ui.m
        if not ui.path("B:/DOSDIR.COM"):
            fail("double-clicking DOSDIR.COM opened no window")

        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosdir: the bracket's text screen:")
        for r in rows[:14]:
            if r.strip():
                print("   | %s" % r.rstrip())

        for line in (l.strip() for l in rows):
            if line.startswith("FAILED") or "FAILED" in line:
                fail("the program reported: %s" % line)

        if "DRIVE B" not in text:
            fail("AH=19h did not answer drive B - the program was launched "
                 "from B: and the map is the identity (SPEC.md 96.6)")
        # SELECTING A DRIVE, which used to be a no-op that reported the
        # count (SPEC.md 96.6.1). The idiom every program uses is select then
        # ask, so a stub answers "invalid drive" for drives that are there.
        for want, why in (("SEL A", "AH=0Eh did not actually move to A: - it "
                                    "answered the drive count and stayed on B:, "
                                    "which is how an installer is told a mounted "
                                    "drive is an invalid letter"),
                          ("BACK B", "AH=0Eh could not get back to B:"),
                          ("NOSUCH B", "selecting a drive that does not exist "
                                       "MOVED the program - the current drive "
                                       "must be left alone, which is what makes "
                                       "the AH=19h after it a real answer")):
            if want not in text:
                fail("%s: expected %r in the program's output" % (why, want))
        print("dosdir: AH=0Eh selects, comes back, and refuses what is not there")

        # A VOLUME LABEL SEARCH MUST NOT ANSWER WITH A FILE (SPEC.md
        # 96.12.1). AH=4Eh's CX is an attribute mask and this box ignored it,
        # so "what is this disk called" was answered with the first ordinary
        # file on it - which is how Prince of Persia was told it was not
        # running from its own floppy.
        lbl = None
        for r in rows:
            if "LABEL" in r:
                lbl = r.strip().split("LABEL", 1)[1].strip()
        if lbl is None:
            fail("the program printed no LABEL line at all")
        for f in ("DOSDIR", "A.TXT", "BB.TXT", "CCC.TXT", "DATA.DAT", "SUBDIR"):
            if f in lbl:
                fail("a volume-label search (AH=4Eh, CX=0008h) answered with "
                     "%r, which is an ordinary directory entry and not a "
                     "label. The attribute mask in CX is being ignored "
                     "(SPEC.md 96.12.1)" % lbl)
        print("dosdir: a volume-label search answers %r, not a file" % lbl)

        if "VEC ok" not in text:
            fail("AH=25h/35h did not round-trip a vector (SPEC.md 96.12)")

        got = None
        for r in rows:
            if r.strip().startswith("FIND "):
                got = r.split()[1:4]
        if got != ["5", "3", "1"]:
            fail("the find counts are %r, not ['5','3','1'] - `*.*` sees five "
                 "files, `*.TXT` three and `?.TXT` one, and no two wrong "
                 "matchers give the same triple (SPEC.md 96.12.1)" % (got,))
        print("dosdir: wildcards: *.* = 5, *.TXT = 3, ?.TXT = 1")

        # --- the date and the time (SPEC.md 96.13) --------------------------
        # MartyPC's 5150 has no clock chip and neither does the machine this
        # project targets, so both the kernel and the DOS box fall back - and
        # the assertion is that they fall back to the SAME DAY. 2026-07-04 is
        # a Saturday, which is DOS's day-of-week 6, and getting Sakamoto's
        # table or its January/February shift wrong moves only that digit.
        date = None
        for r in rows:
            if r.strip().startswith("DATE "):
                date = r.split()[1:3]
        if date != ["2026-7-4", "6"]:
            fail("AH=2Ah answered %r, not ['2026-7-4', '6'] - the fallback "
                 "date is mirrored from kernel/clock.inc so a DOS program and "
                 "the menu bar agree, and 4 July 2026 is a Saturday "
                 "(SPEC.md 96.13.1)" % (date,))
        print("dosdir: AH=2Ah = 2026-07-04, day 6 (Saturday)")

        if "DVAL ok" not in text:
            fail("AH=2Bh accepted month 13 - an impossible date answers FFh")

        tm = None
        for r in rows:
            if r.strip().startswith("TIME "):
                tm = r.split()[1]
        if tm != "13:45:30":
            fail("AH=2Dh then AH=2Ch answered %r, not '13:45:30' - a program "
                 "that sets the clock and reads it straight back has to agree "
                 "with itself (SPEC.md 96.13.2)" % (tm,))
        print("dosdir: AH=2Dh/2Ch round-tripped 13:45:30")

        inside = None
        for r in rows:
            if r.strip().startswith("IN "):
                inside = r.split()[1]
        if inside != "0":
            fail("a find inside the freshly made SUBDIR saw %r entries, not "
                 "'0' - '.' and '..' are not reported to a package, which is "
                 "the fact AH=47h's whole design rests on (SPEC.md 96.12.2)"
                 % (inside,))
        print("dosdir: a fresh subdirectory reports 0 entries, as it must")

        cwd = None
        for r in rows:
            if r.strip().startswith("CWD "):
                cwd = r.strip()[4:].strip()
        if cwd != "SUBDIR":
            fail("AH=47h answered %r, not 'SUBDIR' - the '..' walk builds the "
                 "path from a name this program just chose, and DOS's answer "
                 "carries no leading backslash (SPEC.md 96.12.2)" % (cwd,))
        print("dosdir: AH=47h walked back to %r" % cwd)

        if "DIR ok" not in text:
            fail("the mkdir/chdir/rmdir round trip did not complete")

        m.type_text("x")
        os88marty.settle(m)
        if "Disk" not in ui.titles():
            fail("the desktop did not come back after the bracket")

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosdir.png", wd, ht, data)
        print("dosdir: build/dosdir.png written")

    print("dosdir: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
