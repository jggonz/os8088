#!/usr/bin/env python3
"""RAD tunes that must be refused, and exactly how (SPEC.md 34.12.1, 96.4.4).

    python3 tests/unit/t_rad.py

Every byte of a tune came off a floppy, and Reality's players check nothing,
so SOUND.DRV's verb 4 validates the whole file before its first use and
`tools/radsim.py` implements the same rules to the byte.  This is the
hostile-file table the two are held to: each row is a file built HERE, by
hand, from SPEC.md 96.4 - not by radsim's fixture composer - and the
`(RADE_*, RADC_*, offset)` triple SPEC.md says it earns.  The offsets are
written as positions in the file each row builds, so a reader can check one
against the procedure in 96.4.4 without running anything.

Wave 3 points the same table at the driver (a `-DRADLOG` build on MartyPC);
until then the row that can disagree with this table is radsim.

A row that is ACCEPTED is also played for a few hundred frames by radsim's
engine for its version: an accepted file that makes the reference replayer
read outside the tune is a hole in the rules, whatever the triple says. And
one accepted file is built to fan out, to hold a frame's WORK to RAD_PNMAX
(SPEC.md 96.4.5) - with a negative control that takes the cap out.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                      # noqa: E402
import radsim                                            # noqa: E402

from radrows import *                                   # noqa: E402,F401,F403
from radrows import ROWS, v2, track, fm, SIG            # noqa: E402


# --- run the table ----------------------------------------------------------
eq(sorted(radsim.RADC.values()), list(range(1, 19)), "radsim's RADC_ details are 1..18",
   "SPEC.md 96.4.4's detail table")
for name, data, want, opl3 in ROWS:
    got = radsim.check(data, opl3=opl3)
    eq(got, want, "row '%s'" % name, "SPEC.md 96.4.4 - the triple verb 4 answers")
    if got is None and want is None:
        try:
            radsim.reference_stream(data, 300, opl3=opl3)
            played = True
        except IndexError as e:
            played = e
        check(played is True, "row '%s' is accepted but its replay reads outside the "
              "tune: %r" % (name, played),
              "an accepted file must never walk the replayer off its end")

# The committed fixtures are accepted, and a v2 one is refused on an OPL2.
for fname, _fn in radsim.FIXTURES:
    path = os.path.join(ROOT, "tests", "fixtures", "rad", fname)
    try:
        data = open(path, "rb").read()
    except OSError:
        check(False, "fixture %s is missing" % path, "tools/radsim.py --make writes it")
        continue
    eq(radsim.check(data), None, "fixture %s validates" % fname)
    if data[0x10] == 0x21:
        eq(radsim.check(data, opl3=False), (NEEDOPL3, 0, 0), "fixture %s on an OPL2" % fname)

# Truncation sweep: every proper prefix of every 2.1 fixture past its version
# byte is refused as CORRUPT (a 2.1 file has an end marker), and no prefix of
# any fixture makes the validator raise anything but a refusal.
for fname, fn in radsim.FIXTURES:
    data = fn()
    wrong = []
    for k in range(17, len(data)):
        try:
            got = radsim.check(data[:k])
        except Exception as e:                           # noqa: BLE001
            wrong.append((k, repr(e)))
            continue
        if data[0x10] == 0x21 and (got is None or got[0] != CORRUPT):
            wrong.append((k, got))
    check(not wrong, "prefixes of %s: %r" % (fname, wrong[:4]),
          "a cut-off 2.1 tune is always RADE_CORRUPT, and never an exception")

# Single-byte corruption sweep over RV2.RAD: whatever a flipped byte does to the
# verdict, an ACCEPTED result must still replay inside the file.
data = bytearray(radsim.FIXTURES[2][1]())
escaped = []
for i in range(0x11, len(data)):
    for mask in (0x80, 0x0F):
        d = bytearray(data)
        d[i] ^= mask
        try:
            if radsim.check(bytes(d)) is None:
                radsim.reference_stream(bytes(d), 60)
        except IndexError:
            escaped.append((i, mask))
check(not escaped, "RV2.RAD with a flipped byte replays off its end at %r" % escaped[:4],
      "SPEC.md 96.4.4 is what keeps the interrupt-time replayer inside the claim")

# The fan-out (SPEC.md 96.4.5, 2.1 deviation 5; 34.13.6). The depth guard caps
# how DEEP riffs nest, not how WIDE: three instruments, each with a speed-1
# riff whose one line plays all nine channels alternating between the other
# two, and a pattern line that plays the first on all nine. It is a VALID file
# - built here from 96.4.3, not by radsim's composer - and without the cap its
# first frame plays ~9^8 notes (the reference player too: 36.5 million writes).
eq(radsim.check(FAN), None, "the fan-out tune validates",
   "it is a legal file; the cap, not the validator, is what bounds it")


class _TooMuch(Exception):
    pass


def fan_frame(pnmax, write_limit):
    writes = [0]

    def wr(r, v):
        writes[0] += 1
        if writes[0] > write_limit:
            raise _TooMuch()
    eng = radsim.EngineV2(FAN, wr)
    eng.pnmax = pnmax
    writes[0] = 0
    try:
        eng.update()
    except _TooMuch:
        return None, writes[0]
    return eng.notes, writes[0]


LIMIT = radsim.RAD_PNMAX * 40           # a note play writes at most ~30 registers
eq(radsim.RAD_PNMAX, 32, "RAD_PNMAX", "SPEC.md 96.4.5 / 34.13.6")
notes, writes = fan_frame(radsim.RAD_PNMAX, LIMIT)
check(notes is not None and notes <= radsim.RAD_PNMAX,
      "the fan-out tune's frame 1 played %r notes and %d writes - past RAD_PNMAX %d"
      % (notes, writes, radsim.RAD_PNMAX),
      "SPEC.md 34.13.6: a frame's work is bounded whatever the file says")
check(notes == radsim.RAD_PNMAX,
      "the fan-out tune played %r notes, so it no longer reaches the cap it tests"
      % notes, "the row has to reach RAD_PNMAX to test it")
# negative control: the same frame with the cap taken out runs past any bound
uncapped, uwrites = fan_frame(1 << 30, LIMIT * 20)
check(uncapped is None,
      "with the cap taken out the fan-out frame stopped at %r notes / %d writes - the "
      "row cannot tell a capped replayer from an uncapped one" % (uncapped, uwrites),
      "negative control")

# ...and the frame that reaches the cap is the tune's LAST (deviation 5): the
# reference stream is the start, that one frame and the stop sequence.
_st, _frames, _sp = radsim.frames_of(radsim.reference_stream(FAN, 200))
eq(len(_frames), 1, "frames the fan-out tune plays before it halts",
   "SPEC.md 96.4.5 deviation 5: a refused note play halts the tune")

# SUSTAINED work (SPEC.md 34.13.3 step 5, 34.13.5, 34.13.6): the per-frame cap
# alone bounds one frame, and RAD_FRMAX frames an interrupt - with the RTC's
# credit re-arming it - multiplied that into a machine held at IF = 0. The
# bound is per BIOS TICK: at most 2 x RAD_PNMAX - 1 note plays and
# WORK_TICK computed reference writes, on both pacer classes, whatever the
# file. Two valid tunes at BPM 300 and speed 1, one line on every frame:
#   FANALL - every line plays the fan-out instrument on all nine channels,
#            so every frame would reach the cap (it must halt, on tick 0)
#   HEAVY  - every line plays it on three, 30 note plays a frame, UNDER the
#            cap: it must play on, and the per-tick budget is what bounds it
WORK_TICK = 2560                        # SPEC.md 34.13.6's figure
# (FAN, FANALL and HEAVY are built in tests/unit/radrows.py, which RADGATE's
# disks also take them from - tests/radgate/mkrows.py)


for name, tune, halts in (("FANALL", FANALL, True), ("HEAVY", HEAVY, False)):
    eq(radsim.check(tune), None, "%s validates" % name, "a legal file")
    eq(radsim.rate_tenths(tune), 1200, "%s's rate" % name, "the rate ceiling")
    for rtc in (False, True):
        cls = "RTC" if rtc else "tick"
        per, halted = radsim.paced(tune, 64, rtc=rtc)
        worst_n = max(p[1] for p in per)
        worst_w = max(p[2] for p in per)
        check(worst_n <= 2 * radsim.RAD_PNMAX - 1,
              "%s on the %s class: %d note plays in one tick" % (name, cls, worst_n),
              "SPEC.md 34.13.6: 2 x RAD_PNMAX - 1 a tick")
        check(worst_w <= WORK_TICK,
              "%s on the %s class: %d computed writes in one tick" % (name, cls, worst_w),
              "SPEC.md 34.13.6: WORK_TICK a tick")
        if halts:
            eq(halted, 0, "%s halts on the %s class at tick" % (name, cls),
               "deviation 5: the first capped frame ends the tune")
        else:
            eq(halted, None, "%s halts on the %s class at tick" % (name, cls),
               "under the cap a heavy tune plays on, slowly")
            check(len(per) == 64 and min(p[0] for p in per) >= 1,
                  "%s on the %s class stopped playing" % (name, cls),
                  "the budget delays frames, it never starves the tune")
        # negative control: round 0's rule - 81 a FRAME, no tick budget, no halt
        per0, _h = radsim.paced(tune, 64, rtc=rtc, budget=False, halt=False, pnmax=81)
        check(max(p[2] for p in per0) > WORK_TICK and
              max(p[1] for p in per0) > 2 * radsim.RAD_PNMAX - 1,
              "%s on the %s class under the per-frame rule stays within the bound "
              "(%d writes) - the row cannot tell the rules apart"
              % (name, cls, max(p[2] for p in per0)), "negative control")

done("t_rad")
