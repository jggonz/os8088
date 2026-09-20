#!/usr/bin/env python3
"""Stage 1's RAM floor, both sides of it (SPEC.md 2.7.1).

    python3 tests/bootfloor.py

`tests/unit/t_bootfloor.py` proves the sector carries the ARITHMETIC this
kernel's ladder implies. It cannot prove the arithmetic is right about a
machine, because it never boots one. This does.

TWO BUILDS, AND THE SECOND IS WHAT MAKES THE FIRST MEAN ANYTHING:

  RAMKB=<floor>      the smallest machine the sector accepts. Must reach a
                     DESKTOP - not merely get past .nomem, because everything
                     the floor is computed from is downstream of it: stage 2
                     copying itself to HEAP_SEG, the kernel's own read landing
                     under it, and a heap with anything left in it.
  RAMKB=<floor - 1>  one kilobyte less. Must print RAM and stop.

Without the second, a bound that had been loosened to nothing - or a knob that
never reached the register - would pass the first for entirely the wrong
reason, and look identical to the change working. tests/dljunk.py's shape and
for its reason exactly.

WHY A KNOB AT ALL: QEMU's SeaBIOS answers 639KB whatever `-m` says and
MartyPC's machine is a 640KB 5150, so neither can be asked a different
question. `RAMKB=<n>` assembles the sector to believe a number
(docs/TESTING.md); the KERNEL still reads the real int 12h for its heap, so
what this moves is the sector and the refusal, which is what is under test.
Read the desktop result as "the floor's arithmetic is sound", not as "this OS
runs in that much" - no machine here has that little to give it.

BOTH BUILDS, BOTH KERNELS. kern_small's floor is the one the plan was written
for (docs/plans/completed/BOOT-LADDER-PLAN.md): guard 5 has asserted it boots on 128KB since
the split, and stage 1 refused it at 129 until SPEC.md 2.7.1. A row that
covered kern_big alone would not have caught that at all.

It leaves a KNOB KERNEL in build/ while it runs and rebuilds the plain one on
the way out, tests/dljunk.py's shape and for its reason - which is also why it
is a soak row and serial.

GLaBIOS, so a container with no IBM ROM in tools/martypc/roms/ can run it.
The floor is in the sector and has nothing to do with the adapter.
"""
import os
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88build
import os88marty                                          # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMG = "build/os8088-360.img"
APPS = "build/apps360.img"


def tree(*knobs):
    """One configuration's PRIVATE build (tools/os88build.py).

    This row used to build each configuration into build/ and put the plain
    kernel back in a `finally` - and it builds FIVE of them (two labels x
    floor and floor-1, plus a probe build to read the sector's own number), so
    it was six full builds and the shared tree carried a RAMKB kernel through
    all of them. The trees are keyed and cached, so a re-run costs none.
    """
    return os88build.tree(*knobs,
                          targets=("os8088-360.img", "apps360.img",
                                   "boot360.bin"))


def floor_kb(bdir):
    """The smallest int 12h KB the BUILT sector accepts.

    Read back out of `build/boot360.bin` rather than recomputed here, and
    that is deliberate: `tests/unit/t_bootfloor.py` already proves the
    sector's immediate matches the kernel's ladder, so a second copy of the
    arithmetic in this file would only ever agree with itself. What this row
    is for is whether the number the sector CARRIES is one a machine can
    actually boot on - so it has to be the sector's number, whatever it is.

    The first version of this file did recompute it, went stale the moment
    SPEC.md 2.7.1 gained its second blob length, and reported a floor 7KB
    below the one under test.
    """
    d = open(os.path.join(bdir, "boot360.bin"), "rb").read()
    hits = [d[i + 1] | (d[i + 2] << 8)
            for i in range(len(d) - 3) if d[i] == 0x3D and d[i + 3] == 0x72]
    if len(hits) != 1:
        raise SystemExit("bootfloor: %d cmp/jb pairs in the sector, not 1 - "
                         "t_bootfloor.py says which" % len(hits))
    return -(-hits[0] // 64)


def boots(kb, extra):
    """(reached_a_desktop, the text on screen) for one RAMKB build."""
    t = tree("RAMKB=%d" % kb, *extra)
    img, apps = t.img("os8088-360.img"), t.img("apps360.img")
    try:
        with os88marty.launch(img, apps=apps, machine=MACHINE) as m:
            return True, "\n".join(m.screen())
    except os88marty.MartyError:
        # No desktop. Come back with no gate and read what IS on the screen:
        # `RAM` is the assertion, and a machine that hung on a blank screen
        # would be a different defect wearing the same verdict.
        with os88marty.launch(img, apps=apps, machine=MACHINE, boot=10) as m:
            return False, "\n".join(m.screen())


for label, extra in (("kern_big", ()), ("kern_small", ("KERN_SMALL=1",))):
    # **THE FLOOR COMES OUT OF THE TREE THIS ROW BOOTS, AND THAT IS THE WHOLE
    # OF WHAT WAS WRONG WITH IT.** It read `tree(*extra)` - the build with NO
    # `RAMKB` - and then booted `tree("RAMKB=%d" % kb, *extra)`, and the two
    # do not carry the same number: measured across ten `ramkb-*` trees and
    # five plain ones, a RAMKB build's sector is handed a HEAP_PARA 64
    # paragraphs higher than the plain build's, so its MEMNEED is exactly 1 KB
    # higher - 7,488 against 7,424 on kern_big, 5,248 against 5,184 on
    # kern_small. So this handed every sector one kilobyte LESS than its own
    # arithmetic demands, the sector printed `RAM`, and the row reported that
    # the kernel does not boot at the floor SPEC.md 2.7.1 computes.
    #
    # It reported it in the most alarming words it had, too - "the bound is
    # too LOW and the arithmetic is wrong in the dangerous direction" - for a
    # kernel that is correct: bisected by hand, kern_big boots at 117 and
    # refuses 116, kern_small boots at 82 and refuses 81, which is each knob
    # build's own floor met exactly.
    #
    # The seed value is arbitrary because the knob's cost does not depend on
    # it (ten trees, n from 116 to 640, all 7,488), and `.want` below asserts
    # that rather than assuming it - so a build where it DID depend on n fails
    # naming the two numbers instead of blaming the kernel.
    kb = floor_kb(tree("RAMKB=%d" % floor_kb(tree(*extra).dir), *extra).dir)
    plain = floor_kb(tree(*extra).dir)
    if kb != plain:
        print("bootfloor: %s: the RAMKB build's floor is %d KB where the "
              "plain build's is %d - this row tests the one it BOOTS"
              % (label, kb, plain))

    up, text = boots(kb, extra)
    check(up, "%s reaches a desktop at its floor, %d KB" % (label, kb),
          "everything the floor is computed from is downstream of the "
          "refusal - stage 2's copy to HEAP_SEG, the kernel's read "
          "landing under it, a heap with anything left. A failure here "
          "means the bound is too LOW and SPEC.md 2.7.1's arithmetic is "
          "wrong in the dangerous direction",
          got=text.strip()[:120] or "(a blank screen)", want="a desktop")

    got = floor_kb(tree("RAMKB=%d" % kb, *extra).dir)
    check(got == kb, "%s: the build booted at %d KB carries that floor"
          % (label, kb),
          "the floor is read from one build and tested on another only if "
          "this holds - if a RAMKB build's MEMNEED depends on the RAMKB "
          "value, the check above asked the wrong question and its verdict "
          "means nothing either way", got="%d KB" % got, want="%d KB" % kb)

    up, text = boots(kb - 1, extra)
    check(not up, "%s refuses %d KB, one below it" % (label, kb - 1),
          "a desktop here means the sector is accepting a machine its own "
          "arithmetic says it cannot fit - or that RAMKB never reached "
          "the compare, which would make the check above pass for no "
          "reason at all",
          got="a desktop", want="a refusal")
    check("RAM" in text, "...and says RAM", "the sector should reach its "
          "own message rather than hanging: a blank screen here is a "
          "different defect with the same verdict",
          got=text.strip()[:120] or "(a blank screen)", want="RAM")

# NO RESTORE: build/ is never written now, so there is nothing to put back.
done("bootfloor")
