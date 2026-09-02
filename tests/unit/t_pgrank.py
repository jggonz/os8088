#!/usr/bin/env python3
"""The purgeable caches are ORDERED, and the order is what decides evictions.

    python3 tests/unit/t_pgrank.py

`mem_shed_one` takes the cheapest record STRICTLY BELOW the claimant's rank,
and `mem_avail_lvl` counts a cheaper cache as free and a dearer one as taken.
So the whole eviction policy of the machine is three high bytes, and it is
correct only if each one is honest about what losing that cache costs:

  MEM_P_WSAVE  TRIV  one window redraws when it is raised (SPEC.md 11.96) -
                     which is what every window did before it existed
  MEM_P_FATW   MED   nine sectors on every volume switch until the window is
                     back, NOT one ~400 ms reload: a shed volume falls back to
                     the pin, only one volume can hold that, so two that
                     alternate evict each other (SPEC.md 18.8.3/18.8.4).
                     18.8.1 measured that state at 45 loads against 3
  MEM_P_DIRW   HIGH  316 int 13h calls against 114 on an install (SPEC.md
                     18.95) - the dearest cache in the system

THIS IS A RATCHET AND NOT A RESTATEMENT.  A rank is one token in a constant,
it has no callers, and getting it wrong is silent in both directions: too low
and the cache is thrown away in front of something cheaper to rebuild, too
high and it survives at the expense of something dearer.  `MEM_P_FATW` shipped
at LOW for exactly one commit on a per-event cost compared against a
whole-install one, which is the mistake this file exists to catch the next
time.

It also checks the two things a rank is useless without: that the ranks are
inside the purgeable range at all (a high byte outside it is an ORDINARY claim
that `mem_shed_one` will never look at, which is how a "cache" silently stops
being one), and that `dsk_fatw_want` asks `mem_avail_lvl` at its OWN rank, so
a FAT window may take the raise caches it outranks.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

MEMORY = "kernel/memory.inc"
DISK = "kernel/disk.inc"

# The order the costs above put them in, cheapest first.  A row is a claim
# about what losing that cache costs, with the § that measured it.
ORDER = [
    ("MEM_P_WSAVE", "one window redraws when raised (SPEC.md 11.96)"),
    ("MEM_P_FATW", "nine sectors a volume switch until it is back - 45 loads "
                   "against 3 on 18.8.1's reference copy, ~17 s"),
    ("MEM_P_DIRW", "316 int 13h against 114 on an install (SPEC.md 18.95), "
                   "~81 s - the dearest cache there is"),
]


def read(rel):
    with open(os.path.join(ROOT, rel), errors="replace") as f:
        return f.read()


def rank_of(src, name):
    """The high byte a MEM_P_* tag is defined with, as its MEM_PG_* name."""
    m = re.search(r"^%s\s+equ\s+(MEM_PG_[A-Z]+)<<8" % name, src, re.M)
    return m.group(1) if m else None


def main():
    mem = read(MEMORY)
    disk = read(DISK)
    levels = ["MEM_PG_TRIV", "MEM_PG_LOW", "MEM_PG_MED", "MEM_PG_HIGH"]

    got = []
    for name, why in ORDER:
        r = rank_of(mem, name)
        check(r in levels, "%s is defined at a PURGEABLE rank" % name,
              "A high byte outside MEM_PG_MIN..MEM_PG_MAX is an ORDINARY "
              "claim - mem_shed_one skips it and mem_avail_lvl counts it as "
              "taken, so the cache silently stops being one. What losing it "
              "costs: " + why,
              got=r, want="one of %s" % ", ".join(levels))
        got.append((name, r if r in levels else None, why))

    for (an, ar, aw), (bn, br, bw) in zip(got, got[1:]):
        if ar is None or br is None:
            continue
        check(levels.index(ar) < levels.index(br),
              "%s ranks strictly below %s" % (an, bn),
              "mem_shed_one takes the cheapest record STRICTLY BELOW the "
              "claimant, so this ordering IS the eviction policy. Reversed or "
              "equal, the machine throws away the dearer cache to keep the "
              "cheaper one. %s costs %s; %s costs %s" % (an, aw, bn, bw),
              got="%s=%s, %s=%s" % (an, ar, bn, br),
              want="%s below %s" % (an, bn))

    # ...and the gate: a cache that asks at the BOTTOM rank counts nothing as
    # free, so it never takes the caches it outranks and its rank is decoration
    m = re.search(r"mov al, (MEM_PG_[A-Z]+)\s*;[^\n]*\n[^\n]*call mem_avail_lvl"
                  r"[\s\S]{0,200}?cmp bx, DSK_FATW_MINK", disk)
    own = rank_of(mem, "MEM_P_FATW")
    check(m is not None and m.group(1) == own,
          "dsk_fatw_want asks mem_avail_lvl at its OWN rank",
          "dsk_rah_want's rule (SPEC.md 50.6.4): a cheaper cache counts as "
          "free and a dearer one does not. At MEM_PG_MIN nothing counts, so "
          "the window never takes the raise caches it outranks and is more "
          "timid than its rank entitles it to be - which is what it did for "
          "one commit. It must match MEM_P_FATW's own rank",
          got=m.group(1) if m else "no mem_avail_lvl before DSK_FATW_MINK",
          want="%s, MEM_P_FATW's own rank" % own)

    return done("pgrank")


if __name__ == "__main__":
    main()
