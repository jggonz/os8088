#!/usr/bin/env python3
"""CLEAR SKIES' objects carry the class flag their MODEL says they should
(SPEC.md 88.13.1).

The Detail Level ladder refuses objects in cs_consider before they are ever
transformed, and two flags say which rungs an object survives: CSO_TERRAIN
for the world's own surface - the hills, the water, the runway - which no
rung refuses, and CSO_ROAD for the roads and bridges, which come back one
rung above None. Both are on the OBJECT rather than the model, because
cs_consider tests them in the word it has already loaded.

The price of that choice is that the classification is written twice - once
in the model and once on every row that uses it - and nothing at run time
would say they disagree. A river without CSO_TERRAIN simply empties at
Detail Level = None, which is exactly the bug this file was extended for:
thirty water objects had no flag and every one of them vanished.

So the model is the authority and this is the check. A model's own header
says what it is:

  * a CS_HILL model, or Paris' hand-written Montmartre  -> TERRAIN
  * header ink CSI_RIVER                                -> TERRAIN (water)
  * no faces and header ink CSI_MARK                    -> ROAD

and the Golden Gate is the one classification a header cannot make: its
towers are solids and its deck a box, but a bridge is a bridge, so its four
objects are named here and checked like the rest.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BY_HAND = {"cs_n_sfo_gg": "CSO_ROAD"}       # the Golden Gate, deck and cables


def main():
    src = sorted(glob.glob(os.path.join(ROOT, "apps", "skies", "csw_*.inc")))
    shared = os.path.join(ROOT, "apps", "skies", "csworld.inc")
    want = {}                                # model -> the flag it implies
    for f in src + [shared]:
        for line in open(f):
            m = re.match(r"\s*(cs_m_\w+):\s*db\s+CSM_\w+,\s*\d+,\s*(\d+),\s*(\d+),"
                         r"\s*(CSI_\w+)", line)
            if m:
                nf, ink = int(m.group(2)), m.group(4)
                if ink == "CSI_RIVER":
                    want[m.group(1)] = "CSO_TERRAIN"
                elif nf == 0 and ink == "CSI_MARK":
                    want[m.group(1)] = "CSO_ROAD"
            h = re.match(r"\s*CS_HILL\s+(\w+)", line)
            if h:
                want[h.group(1)] = "CSO_TERRAIN"
    want["cs_m_hill"] = "CSO_TERRAIN"        # Montmartre, older than the macro
    if len(want) < 20:
        sys.exit("t_csterrain: only %d classified models - has a macro or a "
                 "header moved?" % len(want))

    bad, seen = [], 0
    for f in src:
        for n, line in enumerate(open(f), 1):
            m = re.match(r"^\s*CS_OBJ\s+(\w+)\s*,(?:[^,;]*,){7}\s*(\S[^;]*?)\s*(;.*)?$",
                         line)
            if not m:
                continue
            model, flags = m.group(1), m.group(2)
            name = line.split(",")[7].strip()
            need = want.get(model) or BY_HAND.get(name)
            if not need:
                continue
            seen += 1
            if need not in flags:
                bad.append("%s:%d  %s (%s) wants %s, has: %s"
                           % (os.path.basename(f), n, model, name, need, flags))
    if bad:
        sys.exit("t_csterrain: object(s) whose flags disagree with their model.\n"
                 "  Without the flag the Detail Level ladder refuses them - a\n"
                 "  river empties, a mountain goes (SPEC.md 88.13.1):\n    "
                 + "\n    ".join(bad))
    print("  csterrain: %d classified model(s), %d object(s), every flag agrees"
          % (len(want), seen))


if __name__ == "__main__":
    main()
