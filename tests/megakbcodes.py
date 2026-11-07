#!/usr/bin/env python3
"""kern_mega: `mega_k09`, kern_mega's own int 09h, stores the ROM's codes for
the keys a ROM's int 09h would - Ctrl on the grey cluster and the keypad,
Pause and Ctrl+Break swallowed whole (docs/plans/KERN-MEGA-PLAN.md 14.145).

    OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/megakbcodes.py

kern_big gets every key from the machine's ROM; kern_mega in protected mode
gets them from `mega_k09` (kernel/mega.inc), which fills the BIOS buffer at
40:1E that `mega_i16` serves. It stored Ctrl+Left as the plain Left (4BE0
where a ROM stores 73E0), so Lemmings' fine scroll (apps/lemmings/
lemmings.c's LEM_K_CLEFT) and every Ctrl+arrow a Windows or Telnet program
reads did nothing; Pause (E1 1D 45 E1 9D C5) was a Ctrl and a Num Lock make
and toggled Num Lock; Ctrl+Break (E0 46) toggled Scroll Lock.

THE ASSERTION reads the ring itself. A slot keeps its word after a reader
takes it, so the word `mega_k09` stored for a key is at the tail it found
(40:1C before the key) whatever the desktop did with it since. For each key:
the word stored there, or that nothing was stored and the shift state at
40:17 is what it was.

THE FAILING CASE (docs/WRITING-TESTS.md 1): on the kernel before 14.145 the
first Ctrl key fails - `ctrl+left stored 4BE0, wanted 73E0` - and Pause
turns Num Lock over.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as om          # noqa: E402

BDA = 0x400
# (combo, the word a ROM's int 09h stores: AH the scan, AL the ASCII)
STORED = (
    ("left", 0x4BE0),               # the control: a plain grey key
    ("ctrl+left", 0x73E0), ("ctrl+right", 0x74E0),
    ("ctrl+home", 0x77E0), ("ctrl+end", 0x75E0),
    ("ctrl+pgup", 0x84E0), ("ctrl+pgdn", 0x76E0),
    ("ctrl+up", 0x8DE0), ("ctrl+down", 0x91E0),
    ("ctrl+kp_left", 0x7300), ("ctrl+kp_right", 0x7400),
    ("ctrl+kp_pgup", 0x8400), ("ctrl+kp_5", 0x8F00),
    ("ctrl+kp_minus", 0x8E00), ("ctrl+kp_plus", 0x9000),
    ("ctrl+a", 0x1E01),             # the ctrl arm's letters, as before
)
# (combo, what it is) - 0x100 is 86Box's Pause (its E1 1D, src/device/keyboard.c),
# E0 46 its Ctrl+Break - stores nothing and leaves 40:17 as it found it
SWALLOWED = (("0x100", "Pause"), ("ctrl+0x146", "Ctrl+Break"))


def word(m, a):
    b = m.read(a, 2)
    return b[0] | b[1] << 8


def one(m, combo):
    """(the word stored for COMBO or None, 40:17 before, 40:17 after)"""
    tail = word(m, BDA + 0x1C)
    sh0 = m.read(BDA + 0x17, 1)[0]
    m.b.tap(combo)
    m.b.cmd("step 150")
    sh1 = m.read(BDA + 0x17, 1)[0]
    if word(m, BDA + 0x1C) == tail:
        return None, sh0, sh1
    return word(m, BDA + tail), sh0, sh1


def main():
    if not os.environ.get("OS88_MEGA"):
        os.environ["OS88_MEGA"] = "1"
    os.environ.setdefault("OS88_MEGA_EMU", "86box")
    bad = []
    m = om.launch(os.path.join(ROOT, "build", "mega.img"),
                  machine="at286_vga", label="megakbcodes")
    with m:
        m.b.cmd("step 500")
        for combo, want in STORED:
            got, _s0, s1 = one(m, combo)
            if got != want:
                bad.append("%s stored %s, wanted %04X" % (
                    combo, "nothing" if got is None else "%04X" % got, want))
            if s1 & 0x0C:
                bad.append("%s left 40:17 = %02X (ctrl/alt held)" % (combo, s1))
        for combo, what in SWALLOWED:
            got, s0, s1 = one(m, combo)
            if got is not None:
                bad.append("%s stored %04X" % (what, got))
            if (s0 ^ s1) & 0x7F:
                bad.append("%s turned 40:17 %02X into %02X" % (what, s0, s1))
        got, _s0, _s1 = one(m, "a")     # ...and swallowed no more than itself
        if got != 0x1E61:
            bad.append("the key after them stored %s, wanted 1E61" % (
                "nothing" if got is None else "%04X" % got))
    for b in bad:
        print("megakbcodes: FAIL " + b)
    if bad:
        return 1
    print("megakbcodes: PASS - %d keys stored as the ROM's, %d swallowed"
          % (len(STORED), len(SWALLOWED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
