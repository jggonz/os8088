#!/usr/bin/env python3
"""A drive letter in a NAME reaches the drive it names (SPEC.md 96.6.2).

THE FOUND COLUMN IS THE ROW.  A search of another drive that comes back with
THIS drive's directory reports `AX=0000 CF=1`-free success - it is
indistinguishable, from the program's side and from every other instrument
here, from a search that worked.  That is how it shipped: standing on B:, this
box answered `A:*.*` with B:'s own directory and answered `C:*.*` on a machine
with no hard disk, both of them successfully.  So the assertion is WHICH FILE,
checked against what is really on each floppy rather than against a literal.

THE `CUR` COLUMN IS THE SECOND HALF, and without it the row is passable by a
wrong fix: a letter must not MOVE the program.  Under DOS it selects which
drive's current directory the name is resolved against and `AH=0Eh` alone
changes drives, so a box that got the search right by leaving the program on
A: would satisfy every other assertion here.

THE HANDLE ROWS ARE THE THIRD, and are what a pattern cannot reach.  A handle
is a NAME here, re-resolved at every window, so a file opened on A: and read
while standing on B: needs the volume written into the record; and the second
read of the A: handle comes AFTER the B: handle has taken the window, which is
the only way to exercise the steal across volumes.

`tests/dostrap/drvname.asm` is the program, and it runs under a real IBM DOS
3.30 unchanged - the reference column in SPEC.md 96.6.2 came off one.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88fat                                                 # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/dosdrvsys.img"
GATE = "build/dosdrv360.img"

ROW = re.compile(r"^(\S+)\s+AX=([0-9A-F]{4}) CF=(\d) CUR=(\d) FOUND=(\S*)")
# `HA = <open AX>/<read AX>:<CF>/<bytes>`, and HA2 has no open of its own.
# The BYTES are the last field; everything before them is there so that a
# failure says WHICH call refused rather than only that one did.
HND = re.compile(r"^(HA|HB|HA2)\s*=\s*(\S+)")


def fail(msg):
    print("dosdrv: FAIL: %s" % msg)
    sys.exit(1)


def names(img):
    """The 8.3 names in an image's root, as a program would see them."""
    out = set()
    for _, _, raw in os88fat.Fat12(img).entries():
        if raw[11] & 0x08 or raw[0] in (0, 0xE5):
            continue                        # the label, and a free slot
        n = raw[:8].decode("ascii", "replace").rstrip()
        e = raw[8:11].decode("ascii", "replace").rstrip()
        out.add(n + "." + e if e else n)
    return out


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    a_only = names(SYS) - names(GATE)
    b_only = names(GATE) - names(SYS)
    if not a_only or not b_only:
        fail("the two floppies must each carry a name the other has not: "
             "A: has %r, B: has %r" % (sorted(a_only)[:4], sorted(b_only)[:4]))

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/DRVNAME.COM"):
            fail("double-clicking DRVNAME.COM opened no window")

        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("DRVNAME READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:14],))

        print("dosdrv: the bracket's text screen:")
        for r in rows[:16]:
            if r.strip():
                print("      | %s" % r.rstrip())

        found, hand = {}, {}
        home = None
        for r in rows:
            r = r.strip()
            if r.startswith("CUR=") and home is None:
                home = r[4]
                continue
            mm = ROW.match(r)
            if mm:
                found[mm.group(1)] = (mm.group(2), mm.group(3), mm.group(4),
                                      mm.group(5))
                continue
            mm = HND.match(r)
            if mm:
                hand[mm.group(1)] = mm.group(2)
                hand[mm.group(1) + "!"] = mm.group(2).rsplit("/", 1)[-1]

    if home is None:
        fail("the program never said which drive it started on")
    for want in ("*.*", "A:*.*", "A:\\*.*", "B:*.*", "B:\\*.*", "C:*.*"):
        if want not in found:
            fail("no line for the pattern %r; got %r" % (want, sorted(found)))

    bad = [(p, v[2]) for p, v in found.items() if v[2] != home]
    if bad:
        fail("a drive letter MOVED the program: started on %s, and these left "
             "it somewhere else: %r. Under DOS the letter selects where the "
             "name resolves; AH=0Eh alone changes drives (SPEC.md 96.6.2)"
             % (home, bad))
    print("dosdrv: ok  - every pattern left the program on drive %s" % home)

    # --- the searches ------------------------------------------------------
    for pat in ("A:*.*", "A:\\*.*"):
        ax, cf, _, name = found[pat]
        if cf != "0" or ax != "0000":
            fail("%s was refused with AX=%s CF=%s, and A: is right there"
                 % (pat, ax, cf))
        if name not in a_only:
            fail("%s found %r, which is not a name A: has and B: has not "
                 "(%r) - the letter was dropped and the search ran on the "
                 "drive the program is standing on (SPEC.md 96.6.2)"
                 % (pat, name, sorted(a_only)[:6]))
    print("dosdrv: ok  - A:*.* found %r, which is on A: alone"
          % found["A:*.*"][3])

    for pat in ("*.*", "B:*.*", "B:\\*.*"):
        ax, cf, _, name = found[pat]
        if cf != "0" or name not in b_only:
            fail("%s found %r (AX=%s CF=%s); B:'s own names are %r"
                 % (pat, name, ax, cf, sorted(b_only)[:6]))
    print("dosdrv: ok  - B:*.* found %r, which is on B: alone"
          % found["B:*.*"][3])

    # --- a drive that is not there -----------------------------------------
    # MEASURED against IBM DOS 3.30, not reasoned: 3, not 15 (SPEC.md 96.6.2).
    for pat in ("C:*.*", "C:\\*.*"):
        ax, cf, _, _ = found[pat]
        if (ax, cf) != ("0003", "1"):
            fail("%s on a machine with no hard disk answered AX=%s CF=%s; "
                 "IBM DOS 3.30 answers AX=0003 CF=1" % (pat, ax, cf))
    print("dosdrv: ok  - C:*.* refused 0003, as a real DOS does")

    # --- and the handles ---------------------------------------------------
    for k in ("HA", "HB", "HA2"):
        if k not in hand:
            fail("no %s line; got %r" % (k, sorted(hand)))
    for k in ("HA", "HB", "HA2"):
        hand[k] = hand[k + "!"]             # ...the bytes; the prefixes are
                                            # for the message, not the compare
    if hand["HA"] == "NONE":
        fail("a file opened as A:AONLY.TXT while standing on B: could not be "
             "read - the handle resolved its name on the wrong volume "
             "(FH_VOL, SPEC.md 96.6.2)")
    if hand["HB"] == "NONE":
        fail("B:BONLY1.TXT could not be read at all")
    if hand["HA"] == hand["HB"]:
        fail("the A: and B: handles delivered the SAME bytes (%s), so one of "
             "them read the other's file" % hand["HA"])
    if hand["HA2"] != hand["HA"]:
        fail("re-reading the A: handle after the B: handle took the window "
             "gave %s where the first read gave %s - dos_fh_take stole it "
             "across volumes and refilled from the wrong one"
             % (hand["HA2"], hand["HA"]))
    print("dosdrv: ok  - A: %s and B: %s, and A: re-read the same after the "
          "window changed hands" % (hand["HA"], hand["HB"]))
    print("dosdrv: PASS")


if __name__ == "__main__":
    main()
