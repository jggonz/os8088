#!/usr/bin/env python3
"""AH=36h answers for the drive AH=0Eh just SELECTED (SPEC.md 96.27.2).

Prince of Persia's `INSTALL.EXE` reads its file list off the floppy, selects
C: with `AH=0Eh`, asks `AH=19h` where it is (C:), and asks `AH=36h DL=3` how
much room C: has. This DOS answered with the FLOPPY's figures - `AX=0002
BX=006A CX=0200 DX=02C9`, the 720KB disk's 106 free clusters of 1KB - and the
installer said *"Not enough space on destination disk"* about a hard disk with
31MB free. IBM DOS 3.30, the same disk and the same VHD, answers `AX=0004
BX=3E45 CX=0200 DX=3E47` at the same call site.

The cause is SPEC.md 96.48: a select moves only the PROGRAM and leaves the
mount to the next NAME, and `AH=36h` carries no name - so it asked whichever
volume happened to be mounted. `dfree.asm` could not see it: it never selects,
and naming a drive that is not the program's took the old code through a path
that DID mount. So `tests/dostrap/dfsel.asm` makes the installer's shape with
no commercial software: stand on B:, select A:, ask about A: as 0 and as 1,
select B: back and ask about B:.

**THE ANSWER IS CHECKED AGAINST THE IMAGES, not against a literal**: every
line's free and total clusters must be what `tools/os88fat.py` - an
independent FAT reader - counts on the floppy that line is about, and the two
floppies are built so those figures cannot coincide. `CUR` is the second half:
`AH=36h` must not move the program, and only `AH=0Eh` may.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88fat                                                 # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosdfsel360.img"

ROW = re.compile(r"^L(\d) D=(\d) AX=([0-9A-F]{4}) BX=([0-9A-F]{4}) "
                 r"CX=([0-9A-F]{4}) DX=([0-9A-F]{4}) CUR=(\d)")


def fail(msg):
    print("dosdfsel: FAIL: %s" % msg)
    sys.exit(1)


def figures(img):
    """(sectors a cluster, free clusters, bytes a sector, total clusters)."""
    v = os88fat.Fat12(img)
    return (v.spc, len(v.free_clusters()), v.bps, v.nclus)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disk" % p)
    want = {1: figures(SYS), 2: figures(GATE)}
    if want[1][1] == want[2][1]:
        fail("the fixture is void: both floppies have %d free clusters, so "
             "an answer about the wrong one would pass" % want[1][1])

    with os88ui.boot(SYS, apps=GATE, quick=True) as ui:
        m = ui.m
        if not ui.path("B:/DFSEL.COM"):
            fail("double-clicking DFSEL.COM opened no window")
        rows = []

        def ready(mm):
            rows[:] = mm.screen() or []
            return any("DFSEL READY" in r for r in rows)
        try:
            os88marty.until(m, ready, "DFSEL READY", poll=0.05, limit=180.0)
        except os88marty.MartyError:
            fail("the probe never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:10],))
        m.type_text("x")

    got = {}
    print("dosdfsel: the probe said:")
    for r in rows:
        r = r.strip()
        mm = ROW.match(r)
        if mm:
            print("      | %s" % r)
            got[int(mm.group(1))] = tuple(int(g, 16) if i in (2, 3, 4, 5)
                                          else int(g)
                                          for i, g in enumerate(mm.groups()[1:],
                                                                1))
    if sorted(got) != [1, 2, 3, 4]:
        fail("expected lines L1..L4, got %r" % sorted(got))
    print("dosdfsel: A: is %r and B: is %r (spc, free, bps, total, off the "
          "images)" % (want[1], want[2]))

    # where the program stands after each ask: B:, then A: (selected), A:,
    # then B: again - AH=0Eh alone moves it
    for ln, cur in ((1, 2), (2, 1), (3, 1), (4, 2)):
        if got[ln][5] != cur:
            fail("after L%d the program is on drive %d and should be on %d: "
                 "AH=36h moved it, or AH=0Eh did not" % (ln, got[ln][5], cur))
    for ln in (1, 2, 3, 4):
        d, ax, bx, cx, dx = got[ln][:5]
        if (ax, bx, cx, dx) != want[d]:
            other = want[3 - d]
            whose = (" - which is the OTHER floppy's: AH=36h asked the volume "
                     "that was MOUNTED and not the drive it was asked about "
                     "(SPEC.md 96.27.2)" if (ax, bx, cx, dx) == other else "")
            fail("L%d asked AH=36h about drive %d and got AX=%04X BX=%04X "
                 "CX=%04X DX=%04X, and that floppy has %r%s"
                 % (ln, d, ax, bx, cx, dx, want[d], whose))
    print("dosdfsel: ok  - all four asks answered for the drive asked about, "
          "and only AH=0Eh moved the program")
    return 0


if __name__ == "__main__":
    sys.exit(main())
