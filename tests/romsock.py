#!/usr/bin/env python3
"""romsock - the socket-check ROM at POST, on a 5150 with no disk to boot.

docs/plans/ROM-PLAN.md 1.3 and 1.7: before os8088 is asked to run out of ROM,
the window has to be proved - five 8KB sockets, U28-U32, answering F4000-FDFFF
- and the layout has to be proved against a BIOS:

  * the header is an option ROM, so the BIOS's scan finds it and calls its
    init, which prints one verdict per socket;
  * that init re-points int 18h, so a machine with nothing to boot lands in
    OUR stub ("insert a system disk") rather than at F600:0000, which is the
    middle of the payload.

Two boots, both on MartyPC's GLaBIOS 5150 (the BIOS the owner's ROM testbed,
5150 #2, runs off a One ROM - docs/FIELD-MACHINES.md):

  A. the ROM `tools/os88rom.py socket` builds. Every socket must read ok, and
     the stub's sentence must be on the screen once the boot has failed.
  B. the same ROM with ONE byte of U30 changed and both sums re-balanced, so
     the BIOS still accepts the header. U30 must read BAD and the other four
     ok. This is the row's negative control: a check that cannot say BAD
     about a socket that is wrong is not a check (docs/WRITING-TESTS.md 1).

No floppy is mounted. GLaBIOS retries and then raises int 18h, which is the
whole of what B's second half needs.
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty  # noqa: E402
import os88rom    # noqa: E402

MACHINE = "os8088_5150_herc_gla"


def text(m):
    return "\n".join(m.screen())


def boot(rom, want_stub):
    """Boot with `rom` and no disk; the screen once POST has spoken and, if
    asked, once the stub has."""
    with os88marty.launch(None, machine=MACHINE, boot=False, rom=rom,
                          label="romsock") as m:
        seen = ""
        # GUEST time (docs/WRITING-TESTS.md 7): POST, then GLaBIOS's floppy
        # retries, which are ~14 guest seconds before int 18h; 60 is the cap
        for _ in range(120):                 # 25 frames = 0.5s at 50 Hz
            m.advance(frames=25)
            seen = text(m)
            if "U32" in seen and (not want_stub or "insert a system disk"
                                  in seen.lower()):
                break
        return seen


def main():
    fails = []
    with tempfile.TemporaryDirectory(prefix="romsock-") as tmp:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                        "os88rom.py"), "socket", "--out", tmp], check=True,
                       stdout=subprocess.DEVNULL)
        good = os.path.join(tmp, "osrom-socket.bin")

        # B's ROM: one byte of U30 (F8000-F9FFF) flipped, the sums mended
        img = bytearray(open(good, "rb").read())
        img[0x4000 + 0x123] ^= 0x40
        img = os88rom.balance(img)
        bad = os.path.join(tmp, "osrom-u30.bin")
        with open(bad, "wb") as f:
            f.write(img)

        a = boot(good, want_stub=True)
        for s in os88rom.SOCKETS:
            if (s + " ok") not in a:
                fails.append("A: %s did not read ok" % s)
        if "insert a system disk" not in a.lower():
            fails.append("A: no disk to boot, and the stub's sentence never "
                         "appeared - int 18h is not ours (ROM-PLAN 1.3 point 1)")
        print("A:", " | ".join(l.strip() for l in a.splitlines()
                               if "U28" in l or "insert" in l.lower()))

        b = boot(bad, want_stub=False)
        if "U30 BAD" not in b:
            fails.append("B: a wrong byte in U30 did not read BAD")
        for s in ("U28", "U29", "U31", "U32"):
            if (s + " ok") not in b:
                fails.append("B: %s should still read ok" % s)
        print("B:", " | ".join(l.strip() for l in b.splitlines()
                               if "U28" in l))

    for f in fails:
        print("FAIL", f)
    if fails:
        sys.exit(1)
    print("romsock: ok - every socket proved, a bad one named, and int 18h "
          "is the ROM's")


if __name__ == "__main__":
    main()
