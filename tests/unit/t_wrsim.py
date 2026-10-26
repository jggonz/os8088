#!/usr/bin/env python3
"""t_wrsim: WaveRack's host reference holds (SPEC.md 122.3-122.5).

    python3 tests/unit/t_wrsim.py

tools/wrsim.py is the reference the package's reader, envelope and view
cache are held to: tests/waverack.py opens every fixture on the guest and
compares WaveRack's bss with wrsim.parse() and wrsim.columns(). A reference
that drifted - a fixture that no longer says what its name says, a parse
that disagrees with Python's own `wave`, an impulse the coarse envelope no
longer shows at Fit - would make that soak row compare the guest with the
wrong answer and pass. So the selfcheck runs here, on every `make`: every
fixture parsed to its expectation, every refusal its own code and a
sentence of at most 22 characters (the COMPACT status field), the accepted
ones read back by `wave` where it can read them, the impulses and the
stereo spike surviving the envelope, and LONG.WAV's block the smallest that
fits 2,048. Standard library only; milliseconds.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "..", "..", "tools")]
import harness                                                # noqa: E402
import wrsim                                                  # noqa: E402

harness.check(wrsim.selfcheck(quiet=True), "tools/wrsim.py --selfcheck",
              why="the reference tests/waverack.py holds WaveRack to has "
                  "drifted: run `python3 tools/wrsim.py --selfcheck` for "
                  "the failing fixture")
# the sentences are the package's words too (wrwav.inc, ww_errs and ww_say)
src = open(os.path.join(HERE, "..", "..", "apps", "waverack",
                        "wrwav.inc")).read()
for code, s in sorted(wrsim.SENTENCE.items()):
    frag = s.split("%")[0].rstrip() if "%" in s and not s.startswith("%") \
        else s.split("%")[-1][1:].strip() if "%" in s else s
    harness.check(frag in src, "wrwav.inc spells E_%d's sentence %r" % (code, s),
                  why="the status bar and the reference must say the same "
                      "words; the soak row compares them on the glass")
harness.done("t_wrsim")
