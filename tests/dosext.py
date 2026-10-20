#!/usr/bin/env python3
"""A TYPED EXTENSION, AND THE ARGUMENTS AFTER IT (SPEC.md 96.33.15.1).

COMMAND.COM's rule has two halves - no extension is a search, and an extension
must be one it can execute - and `dos_con_ext` shipped with a check that
answered NO to both.  `mov ah, al` banked the literal one instruction before
the `pop ax` that restored the register it was banked into, so the compare read
a byte nobody had set and every dotted name on the machine came back `Bad
command or file name`: `PRINCE.EXE`, `DOSARGS.COM`, a fully qualified
`B:\\BIN\\FOO.COM` - the most natural spelling of "run this program".

**THE REASON IT SHIPPED IS THE ROW AND NOT THE REGISTER.**  tests/doscon.py
asserts that `DOS.O88` is REFUSED, which a check stuck saying no passes
perfectly.  A negative case on its own cannot tell a working rule from a broken
one, so this row asserts both halves of it - and the negative control is the
sharp part: `BIN/DOSARGS.DAT` is a BYTE-FOR-BYTE COPY of `BIN/DOSARGS.COM`, so
the refusal cannot be about the file being missing, being empty, or not being a
program.  The same bytes run under one name and are refused under the other,
and the only difference is three characters.

The six spellings, all of the same program, in one boot:

  DOSARGS                  runs, no arguments   - 96.33.7's search
  DOSARGS.COM              runs, no arguments   - THE HALF NOBODY ASSERTED
  DOSARGS /M P:220         runs, with them
  DOSARGS.COM /M P:220     runs, with them      - the reported defect
  B:\\BIN\\DOSARGS.COM /M    runs, with them      - fully qualified, for free
  DOSARGS/M                runs, with them      - the switch with no space
  DOSARGS.COM/M            runs, with them        in front of it (96.33.15.2)
  DOSARGS.DAT              REFUSED              - the rule's own half

96.33.15.3 records the one spelling DOS still takes and this box does not -
a path with the extension left off - so a reader who finds it refused knows it
was measured rather than missed.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dosmap                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
ARGS = "build/dosargs360.img"
BOX = "A:/APPS/DOS.O88"

# (what is typed, what the program must report as its arguments, or None for
#  "the console must refuse this line")
CASES = [
    ("DOSARGS",                 "(none)"),
    ("DOSARGS.COM",             "(none)"),
    ("DOSARGS /M P:220",        "/M P:220"),
    ("DOSARGS.COM /M P:220",    "/M P:220"),
    ("B:\\BIN\\DOSARGS.COM /M",  "/M"),
    ("DOSARGS/M",               "/M"),
    ("DOSARGS.COM/M",           "/M"),
    ("DOSARGS.DAT",             None),
]


def fail(msg):
    print("dosext: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, ARGS):
        if not os.path.exists(p):
            fail("%s is missing - `make %s` builds the gate disk"
                 % (p, os.path.basename(p).split(".")[0]))

    with os88ui.boot(SYS, apps=ARGS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch the DOS box")
        dm = dosmap.package()

        def seg():
            """The instance, RE-RESOLVED every read: dos_run claims the arena
            and the region is movable, so a segment banked once decodes as
            plausible nonsense the moment a launch compacts the heap."""
            for sl in range(8):
                wp = os88geom.winptr(m, sl, m.sym)
                ws = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
                ttl = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
                if ws and ttl and m.read(ws * 16 + ttl, 4).startswith(b"DOS\0"):
                    return ws
            fail("the DOS window has gone from the table")

        def console():
            scr = m.read((seg() << 4) + dm["con_scr"], 80 * 25 * 2)
            out = []
            for r in range(25):
                row = scr[r * 160:(r + 1) * 160]
                out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                                   for i in range(0, 160, 2)).rstrip())
            return [r for r in out if r.strip()]

        def typ(s):
            m.type_text(s)
            os88marty.settle(m)

        typ("B:\n")
        typ("CD BIN\n")
        if not console()[-1].endswith("BIN>"):
            fail("the prompt is %r and not B:\\BIN> - every case below is typed "
                 "standing in the folder the program is in"
                 % console()[-1])

        bad = []
        for line, want in CASES:
            typ(line + "\n")
            seen = {}

            def answered(mm):
                scr = [y.rstrip() for y in (mm.screen() or []) if y.strip()]
                if any("READY" in y for y in scr):
                    named = [y[5:] for y in scr if y.startswith("ARGS ")]
                    seen["v"] = ("ran", named[-1] if named else "?")
                    return True
                if any("Bad command" in y for y in console()[-3:]):
                    seen["v"] = ("refused", None)
                    return True
                return False
            # the answer is the GUEST's to give, so the deadline is its clock
            try:
                os88marty.until(m, answered, "%r to run or be refused"
                                % line, poll=0.3, limit=35.0)
            except os88marty.MartyError:
                pass
            verdict, got = seen.get("v", (None, None))
            if verdict == "ran":
                m.type_text("x")
                os88marty.settle(m)
            if verdict is None:
                bad.append("%r: neither ran nor was refused inside 105 guest "
                           "seconds" % line)
            elif want is None and verdict != "refused":
                bad.append("%r: the console RAN it, and DOSARGS.DAT is the "
                           "same bytes as DOSARGS.COM under a name "
                           "COMMAND.COM will not execute (96.33.15)" % line)
            elif want is not None and verdict != "ran":
                bad.append("%r: the console refused it, and it names a program "
                           "that is there (96.33.15.1)" % line)
            elif want is not None and got != want:
                bad.append("%r: the program was given %r and not %r"
                           % (line, got, want))
            else:
                print("dosext: %-24s -> %s%s"
                      % (line, verdict, "" if got is None else "  ARGS %s" % got))
            sys.stdout.flush()

        if bad:
            fail("%d of %d spellings answered wrongly: %s"
                 % (len(bad), len(CASES), "; ".join(bad)))

    print("dosext: ok - six spellings, and the rule says yes and no to the "
          "right ones")
    return 0


if __name__ == "__main__":
    sys.exit(main())
