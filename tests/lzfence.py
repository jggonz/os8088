#!/usr/bin/env python3
"""OSAPI_DECOMP refuses a hostile stream (SPEC.md 20.13.4).

docs/plans/O88-COMPRESSION-PLAN.md 13 makes this wave 1's gate. The bounds in
kernel/lz.inc were measured for SIZE and SPEED before they were ever fed a bad
stream, and "it refuses" was an assertion until this row existed.

Read out of the package's own IMAGE rather than off the screen: the window is
drawn too and a screenshot would prove it was drawn, not what it says.

It also checks the one thing the package cannot check about itself - that a
REFUSED call wrote nothing past the length it was given. The package poisons
its claim with 0xCC first, so anything the decoder touched past the twelve
bytes it was told about is still visible afterwards.
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

S = os88sym.linear
MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}
TAGS = ("control", "truncated", "zero offset", "offset past output",
        "overlong match")


def say(*a):
    print(*a, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    a = ap.parse_args()

    # THE PATHS AND NOT THE TARGET NAME: `Row(wants=...)` carries
    # what `make <path>` produces, and the runner builds it before
    # any row starts - a phony name never satisfies the existence
    # check that follows. Same build, and sayable.
    need("build/lzfence360.img")            # `all` builds nothing under tests/
    img = os.path.getsize(os88build.at("build/lzfence.bin"))
    fails = []
    with os88marty.launch("build/os8088-360.img",
                          apps="build/lzfence360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzfence: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LZFENCE.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("lzfence: LZFENCE.O88 did not open a window - the entry "
                     "proc refused, which is itself a failure")
        rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("lzfence: package at %04X" % pseg)

        raw = m.readseg(pseg, 0, img)
        for tag in TAGS:
            i = raw.find(b" " + tag.encode() + b"\0")
            if i < 4:
                sys.exit("lzfence: %r is not in the image" % tag)
            verdict = raw[i - 3:i].decode("ascii", "replace")
            say("  %-20s %s" % (tag, verdict))
            if verdict != "ok ":
                fails.append("%s: %r" % (tag, verdict))

        # ...and nothing was written past the length the caller declared.
        claim = int.from_bytes(m.readseg(pseg, img, 2), "little")
        if claim:
            tail = m.readseg(claim, 12, 64)
            if any(b != 0xCC for b in tail):
                fails.append("a refused call wrote past the declared output: "
                             "%r" % tail[:16])
            else:
                say("  %-20s ok  (64 bytes past the output are still poison)"
                    % "wrote nothing past")

    for f in fails:
        say("  FAIL: " + f)
    say("lzfence: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
