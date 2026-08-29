#!/usr/bin/env python3
"""Every kernel owner tag has a NAME on the Task Manager's heap page.

    python3 tests/unit/t_ktags.py

SPEC.md 28.4's TYPE column decodes `CLS_OWN`, and its last table row is a
deliberate honesty rule: *"anything else prints the raw owner word, in hex"*,
because a debug page must not label an unknown tag as the nearest thing it
recognises.  That rule is about a tag this build has never seen.  It is NOT a
place to leave a tag the kernel ships, and three of them had been sitting
there:

    MEM_K_MOD    0xFF08   an on-demand kernel module's image  (SPEC.md 2.8)
    MEM_K_CLONE  0xFF09   the disk cloner's buffer            (SPEC.md 18.99)
    MEM_K_BAND   0xFF0A   the 1bpp band composer's buffer     (SPEC.md 5.9.2)

`MEM_K_BAND` is the one that says why this is a gate rather than a note.  It
is claimed ONCE AT BOOT and never freed, so `FF0A` was not a rare row you
might catch during a clone - it was a permanent row on the heap page of every
machine that has a band composer, and the page is the one place in the OS that
exists to explain the heap to a human.

THE FAILURE IS SILENT IN BOTH DIRECTIONS, which is what nothing else here
catches.  Adding a tag to `kernel/memory.inc` and not to `apps/os88api.inc`
assembles cleanly (the Task Manager never names the symbol) and prints a
number.  Adding it to the SDK and not to `tm_ktab` assembles cleanly too, and
prints the same number.  `t_mirror.py` cannot see either: it only compares
constants that are ALREADY written down twice, and a missing mirror is exactly
a constant that is not.

THE LIST MAINTAINS ITSELF, which is the point and the reason this is not a
hand-written table of ten names: it takes every `MEM_K_*` / `MEM_P_*` out of
`kernel/memory.inc` and requires each one to reach `tm_ktab` - so a tag added
tomorrow is covered tomorrow, with nobody remembering to come back here.

TWO THINGS IT DELIBERATELY DOES NOT REQUIRE, both stated so nobody reads a
pass as more than it is:

  * `MEM_P_WSAVE` is a RANGE (SPEC.md 11.96.3), one cache per window slot, and
    `tm_htype` tests it BEFORE it walks `tm_ktab` - so it is named in the
    module without being a row of the table.  RANGE_TAGS below carries it, and
    the check for it is that the module names it at all.
  * `MEM_P_WSAVE_N` is that range's LENGTH rather than an owner word.  A name
    ending `_N` is not a tag.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

KERNEL = "kernel/memory.inc"
SDK = "apps/os88api.inc"
TASKMGR = "apps/taskmgr/taskmgr.asm"

# Tags that name a BLOCK of owner words rather than one, and so are decoded
# before tm_ktab is walked rather than out of it.  A row here is a decision:
# say which range and why it cannot be a table row.
RANGE_TAGS = {
    "MEM_P_WSAVE": "a window's raise cache: MEM_P_WSAVE_N of them, the slot "
                   "added (SPEC.md 11.96.3), so tm_htype subtracts and "
                   "compares against the length instead of matching a word",
}

EQU = re.compile(r"^(MEM_[KP]_[A-Z0-9_]+)\s+equ\s", re.M)


def read(rel):
    with open(os.path.join(ROOT, rel), errors="replace") as f:
        return f.read()


def main():
    kern = read(KERNEL)
    sdk = read(SDK)
    tm = read(TASKMGR)

    # tm_ktab's own body: the (owner word, name) pairs, ended by a 0 owner.
    m = re.search(r"^tm_ktab:\n((?:\s+dw .*\n)+)", tm, re.M)
    if not check(m is not None, "tm_ktab is where this check expects it",
                 "the table moved or was renamed - this gate is now watching "
                 "nothing at all, which is worse than not having it"):
        done("ktags")
    ktab = m.group(1)

    tags = [t for t in EQU.findall(kern) if not t.endswith("_N")]
    check(len(tags) >= 10, "kernel/memory.inc still defines its owner tags",
          "a rename of the MEM_K_ / MEM_P_ prefix would empty this check "
          "without failing it", got=len(tags), want=">= 10")

    for tag in tags:
        # 1. the SDK mirrors it, so the Task Manager can name the symbol...
        check(re.search(r"^%s\s+equ\s" % tag, sdk, re.M) is not None,
              "%s is mirrored into %s" % (tag, SDK),
              "the Task Manager reads the claim table through the SDK and "
              "cannot name a tag that is not in it - the heap page then "
              "prints the owner word in hex, which is SPEC.md 28.4's rule for "
              "an UNKNOWN tag and not for one this kernel ships")

        # 2. ...and something in the module actually decodes it.
        if tag in RANGE_TAGS:
            check(tag in tm, "%s is decoded by %s (a range: %s)"
                             % (tag, TASKMGR, RANGE_TAGS[tag]),
                  "tm_htype tests the range before it walks tm_ktab; if the "
                  "module has stopped naming it, every window's raise cache "
                  "is back to printing as a number")
            continue
        check(re.search(r"\b%s\b" % tag, ktab) is not None,
              "%s has a TYPE name in tm_ktab" % tag,
              "SPEC.md 28.4's hex fallback is for a tag this build has never "
              "seen, not for one the kernel ships. MEM_K_BAND is claimed at "
              "boot and never freed, so leaving it out put a permanent 'FF0A' "
              "row on the heap page of every machine")

    done("ktags")


main()
