#!/usr/bin/env python3
"""An opening's SEAM (tools/os88vbt.seam_join, SPEC.md 126.2): two openings
cut from one clip, each exported with its own few milliseconds of silence at
the cut, play a drop between them that the source never had - the owner's
Demo Battle 2, 17.1 ms at 01-Opening's end and 10.2 ms at 02-Opening's
start, 26.8 ms of exact silence out of the card on the PicoMEM 2 twin.

    python3 tests/unit/t_vbtseam.py

Checked, on synthetic sounds at 735 samples a frame:
  - auto closes that seam whole: no quiet sample across it, the first
    opening's length kept, the second's sound the 301 samples earlier;
  - a WHOLE silent head frame that is black is trimmed, then the rest shifted;
    the same frame NOT black is left alone (and so is the seam: over a frame);
  - "none" changes nothing; "trim" alone shifts nothing; "shift" alone trims
    nothing.

Broken on purpose (seam_join returning A and B untouched) the first check
goes red.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "tools"))
import os88vbt as V     # noqa: E402

SPF = 735
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        fails.append(what)


a = np.concatenate([np.full(236 * SPF - 189, 0.5), np.zeros(189)])
b = np.concatenate([np.zeros(112), np.full(67 * SPF - 112, 0.3)])
sa, sb, drop, shift = V.seam_join(a, 236, b, 67, SPF, "auto", lambda i: True)
joined = np.concatenate([sa, sb])
ok(drop == 0 and shift == 301, "auto: no frame trimmed, 301 samples shifted "
   "(%d, %d)" % (drop, shift))
ok(len(sa) == 236 * SPF, "the first opening keeps its length")
ok(np.all(np.abs(joined[:236 * SPF + 100]) > V.SEAM_QUIET),
   "no quiet sample across the seam")

b2 = np.concatenate([np.zeros(SPF + 100), np.full(66 * SPF, 0.3)])
r = V.seam_join(a, 236, b2, 67, SPF, "auto", lambda i: True)
ok(r[2:] == (1, 289), "a silent black head frame trimmed, then 289 shifted "
   "(%d, %d)" % r[2:])
r = V.seam_join(a, 236, b2, 67, SPF, "auto", lambda i: False)
ok(r[2:] == (0, 0), "the same frame not black: left as it is")
r = V.seam_join(a, 236, b, 67, SPF, "none", lambda i: True)
ok(r[2:] == (0, 0) and r[0] is a and r[1] is b, "none changes nothing")
r = V.seam_join(a, 236, b2, 67, SPF, "trim", lambda i: True)
ok(r[2:] == (1, 0), "trim alone shifts nothing")
r = V.seam_join(a, 236, b, 67, SPF, "shift", lambda i: True)
ok(r[2:] == (0, 301), "shift alone")
sys.exit(1 if fails else 0)
