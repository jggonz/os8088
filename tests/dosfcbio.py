#!/usr/bin/env python3
"""WordPerfect 4.1's DOS calls: AH=33h, AH=37h and the FCB file calls
(SPEC.md 96.56).

WordPerfect 4.1 left this DOS with exit code 0 and not a word printed: it
asks `AH=33h AL=0`, then sets the break with `mov al, 1 / int 21h` on the AH
it was handed back, and the invalid-function arm's AX=0001 made that AH=00h,
TERMINATE. Behind that it is an FCB program from its first overlay read to
its last delete. `tests/dostrap/fcbio.asm` asks every one of those calls once
and prints one line per question, and the table below is what IBM DOS 3.30
printed for the same binary - measured, not derived. The one line that may
differ is the found name, which is whichever of the probe's two files the
directory holds first.

No WordPerfect is needed or used: the binary is commercial and is not in
this tree, and the probe asks what it asks.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosfcbio360.img"

# IBM DOS 3.30, the same FCBIO.COM, line for line
WANT = [
    "33=3300 DL=00",
    "33=3301",
    "37=3700 DL=2F",
    "16=00 10=00",
    "16=00 28=00 CX=0009 10=00",
    "0F=00 SZ=0009 RS=0080",
    "27=01 CX=0009  HELLO FCB",
    "27=03 CX=0003 PAD=0000",
    "10=00",
    "11=00  FCBTST  ",          # ...and TV1 or CHK
    "13=00 11=FF",
    "0F=FF",
]


def fail(msg):
    print("dosfcbio: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE, quick=True) as ui:
        m = ui.m
        if not ui.path("B:/FCBIO.COM"):
            fail("double-clicking FCBIO.COM opened no window")
        rows = []

        def ready(_m):
            rows[:] = m.screen() or []
            return any(r.strip() == "KEY" for r in rows)
        try:
            os88marty.until(m, ready, "FCBIO's KEY", poll=0.05, limit=120)
        except os88marty.MartyError:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:16],))
        print("dosfcbio: the bracket's text screen:")
        for r in rows:
            if r.strip():
                print("      | %s" % r.rstrip())

    lines = [r.rstrip() for r in rows if r.strip()]
    try:
        i = next(k for k, r in enumerate(lines) if r.startswith("33="))
    except StopIteration:
        fail("no `33=` line: the probe printed nothing of its own - a program "
             "that ends at once is AH=33h's AX=0001 turning `mov al, 1` into "
             "AH=00h, TERMINATE")
    got = lines[i:i + len(WANT)]
    for want, have in zip(WANT, got + [""] * len(WANT)):
        ok = have.startswith(want) if want.endswith("  ") else have == want
        if want.startswith("11=00") and ok:
            ok = have[len(want):] in ("TV1", "CHK")
        if not ok:
            fail("IBM DOS 3.30 prints %r here and this DOS printed %r "
                 "(SPEC.md 96.56)" % (want.rstrip(), have))
        print("dosfcbio: ok  - %s" % have)
    print("dosfcbio: PASS")


if __name__ == "__main__":
    main()
