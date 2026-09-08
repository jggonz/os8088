#!/usr/bin/env python3
"""A COMPRESSED FILE is read transparently (SPEC.md 20.14).

docs/plans/O88-COMPRESSION-PLAN.md 13 wave 5. The disk carries one document twice -
PLAIN.TXT as it is, PACKED.TXT inside a 'CZ' wrapper - so the package's own
assertions are the two of them compared with each other, and this script only
has to read the eight verdicts out of its image.

Comparing two files on the machine, rather than one file against a checksum
carried from here, is what keeps the fixture maintainable: the text can be
rewritten without touching a line of test code, and a decoder that got the
last run wrong fails `read` rather than passing a length check.

The last two verdicts are the ones the write half turns on:

  stamp   PACKED.TXT's RAW bytes, written back out under another name, come
          back EXPANDED - so the hint is derived from the bytes being written
          and a copy of a compressed file is still a compressed file. Without
          this, the file manager's copy would produce a file that reads as
          1,943 bytes of gibberish where the original reads as 2,682 of text
  clear   ...and a PLAIN file written over that same name reads back plain. A
          stale mark would send prose to the decoder, which refuses it

The seventh and eighth are the TIGHT BUFFER: a 4,096-byte file, wrapped LZB
and wrapped LZ4, each read into a capacity of exactly 4,096 - the case that
used to need a sliding window for one format and be refused for the other,
and that a stream's own raw tail now makes ordinary (SPEC.md 20.13.7).

**AND THEN IT OPENS README.TXT**, on the shipped system disk, by
double-clicking it - which is the second half of this row and is not a
duplicate of the eight above. The manual's reader has 16,384 bytes for its
text, and in-place expansion once wanted 64 more than the text. That is
SPEC.md 20.14.2.1, it is what the field reported as *"Note Pad says Too big"*
on the day the manual first shipped compressed, and no assertion over a
fixture could have caught it: the defect is in the arithmetic between the
size an application is TOLD and the size the read NEEDS, so the only thing
that finds it is a real reader whose buffer is sized to the first.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import dispcp                                          # noqa: E402
from os88fixture import need                           # noqa: E402
from trackmove import pkg_syms                         # noqa: E402

S = os88sym.linear
MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}
TAGS = ("read", "find", "plainrec", "raw", "stamp", "clear",
        "window", "tight")


def say(*a):
    print(*a, flush=True)


# What the manual is, on the host, so nothing here carries a number that the
# next edit of it makes wrong. THE SOURCE AND NOT build/readme-plain.txt: the
# Makefile writes CRLF onto the disk and np_load FOLDS it straight back off
# (SPEC.md 27.11), so what Note Pad ends up holding is the LF file byte for
# byte - 16,019 where the disk file is 16,334. Which makes this a better
# assertion than a length: 315 carriage returns had to arrive to be dropped.
PLAIN = "readme.txt"


def readme(m, mo, before, fails):
    """README.TXT, off the SHIPPED system disk, opened by a double-click.

    SPEC.md 20.14.2.1's case and the one a fixture cannot be: Note Pad claims
    NP_MAXKB = 16,384 bytes for a document the machine reports as 16,334, and
    in-place expansion wants 16,413 of them. The kernel takes a scratch claim
    rather than answering FERR_BIG, and `np_len` is what says it worked - a
    window with a title and an empty note looks identical to a window with the
    file in it, at every zoom.
    """
    want = os.path.getsize(os.path.join(
        os.path.dirname(__file__), "..", PLAIN))
    disk = os.path.getsize(os.path.join(
        os.path.dirname(__file__), "..", "build", "readme-plain.txt"))
    P = pkg_syms("apps/notepad/notepad.asm")
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "README.TXT")
    try:
        os88marty.until(m, lambda mm: len(dispcp.win_list(mm, S)) > len(wins),
                        "Note Pad's window", poll=0.2, guest=40.0)
    except os88marty.MartyError:
        pass
    after = dispcp.win_list(m, S)
    if len(after) <= len(wins):
        fails.append("README.TXT opened no window - the association did not "
                     "run, or Note Pad refused the file outright")
        return
    rec = m.read(S("wm_wins") + after[-1] * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    pseg = rec[22] | (rec[23] << 8)
    # ...and the READ, which is the assertion: np_len going non-zero is the
    # 16KB expanding into the scratch claim, so it is waited for on the
    # guest's clock rather than given four host seconds.
    try:
        os88marty.until(
            m, lambda mm: int.from_bytes(mm.readseg(pseg, P["np_len"], 2),
                                         "little"),
            "Note Pad to hold the file", poll=0.1, guest=30.0)
    except os88marty.MartyError:
        pass
    got = int.from_bytes(m.readseg(pseg, P["np_len"], 2), "little")
    say("  readme     %s  (np_len=%d, the %d-byte CRLF file folded, Note Pad "
        "at %04X)" % ("ok " if got == want else "BAD", got, disk, pseg))
    if got != want:
        fails.append("Note Pad holds %d bytes of README.TXT and the folded "
                     "file is %d (%d on the disk) - FERR_BIG reads as an "
                     "EMPTY note, which is what 20.14.2.1's scratch claim "
                     "exists to prevent" % (got, want, disk))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    a = ap.parse_args()

    # THE PATHS AND NOT THE TARGET NAME: `Row(wants=...)` carries
    # what `make <path>` produces, and the runner builds it before
    # any row starts - a phony name never satisfies the existence
    # check that follows. Same build, and sayable.
    need("build/lzfile360.img")             # `all` builds nothing under tests/
    img = os.path.getsize(os88build.at("build/lzfile.bin"))
    fails = []
    with os88marty.launch("build/os8088-360.img",
                          apps="build/lzfile360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzfile: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LZFILE.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("lzfile: LZFILE.O88 did not open a window - the entry "
                     "proc refused, which is itself a failure")
        rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("lzfile: package at %04X" % pseg)

        raw = m.readseg(pseg, 0, img)
        for tag in TAGS:
            i = raw.find(b" " + tag.encode() + b"\0")
            if i < 4:
                sys.exit("lzfile: %r is not in the image" % tag)
            verdict = raw[i - 3:i].decode("ascii", "replace")
            say("  %-10s %s" % (tag, verdict))
            if verdict != "ok ":
                fails.append("%s: %r" % (tag, verdict))

        readme(m, mo, wins2, fails)

    for f in fails:
        say("  FAIL: " + f)
    say("lzfile: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
