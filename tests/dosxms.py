#!/usr/bin/env python3
"""The DOS XMS gate (SPEC.md 96.15).

ON AN 8088 THE WHOLE ASSERTION IS A REFUSAL, and it is worth a row because
getting it wrong is silent in both directions.

  - `int 2Fh AX=4300h` must answer AL != 80h when the pool can hand nothing
    out. A shim that says "yes" here and then refuses every call leaves the
    program worse off than one that says no: by the time the first refusal
    arrives the program has already committed to XMS (SPEC.md 96.15.1).
  - Every OTHER multiplex number must answer AL = 0, "nobody is here". That
    is a thing an UNHOOKED vector cannot say, and it is most of what hooking
    int 2Fh buys at all.
  - And asking has to RETURN. An unhooked int 2Fh on a ROM that does not
    implement it is how a TSR probe becomes a hang, and this row's third
    line is the program still running afterwards.

WHAT THIS ROW DOES NOT COVER, stated because SPEC.md 96.15.3 states it: the
WORKING path - allocate, move out, move back, free - needs a 286 or better
with XMEM.DRV mounted, which MartyPC cannot be (docs/TESTING.md's QEMU list,
entry 1). No machine in this tree has run an XMS allocation through that
code. A QEMU twin of this row is what would change that.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
XMS = "build/dosxms360.img"


def fail(msg):
    print("dosxms: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, XMS):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=XMS) as ui:
        m = ui.m
        if not ui.path("B:/DOSXMS.COM"):
            fail("double-clicking DOSXMS.COM opened no window")

        rows = []
        end = time.time() + 120.0
        while time.time() < end:
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished - an int 2Fh that does not come "
                 "back is exactly what this row exists to catch. The last "
                 "text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:10],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosxms: the bracket's text screen:")
        for r in rows[:12]:
            if r.strip():
                print("   | %s" % r.rstrip())

        got = {}
        for r in rows:
            t = r.split()
            if len(t) >= 2 and t[0] in ("XMS4300", "MUX1600"):
                got[t[0]] = t[1]

        if got.get("XMS4300") == "80":
            fail("int 2Fh AX=4300h answered 80h on a machine with NO extended "
                 "memory - a program told yes here has committed to XMS by "
                 "the time the first call refuses (SPEC.md 96.15.1)")
        if got.get("XMS4300") is None:
            fail("the program never printed its AX=4300h answer")
        print("dosxms: AX=4300h -> AL=%s, which is 'no driver'"
              % got["XMS4300"])

        if got.get("MUX1600") != "00":
            fail("an unrelated multiplex number answered AL=%s, not 00 - "
                 "'nobody is here' is the whole of what hooking int 2Fh buys "
                 "(SPEC.md 96.15.1)" % got.get("MUX1600"))
        print("dosxms: an unrelated multiplex number -> AL=00")

        if "ALIVE" not in text:
            fail("the program did not survive its own int 2Fh calls")

        m.type_text("x")
        os88marty.settle(m)
        if "Disk" not in ui.titles():
            fail("the desktop did not come back after the bracket")

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosxms.png", wd, ht, data)
        print("dosxms: build/dosxms.png written")

    print("dosxms: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
