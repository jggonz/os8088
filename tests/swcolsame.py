#!/usr/bin/env python3
"""sw_col's whole-column store must change NOTHING on the glass (SPEC.md 39.25).

    make NOCOLFAST=1 && python3 tests/swcolsame.py build/sc-off.bin
    make             && python3 tests/swcolsame.py build/sc-on.bin
    cmp build/sc-off.bin build/sc-on.bin

    ...and again with --machine os8088_5150_herc_gla, which is a different
    banked layout (SPEC.md 39.3) and therefore a different wrap in the row
    step this body inlines.

THE COMPARISON IS THE WHOLE TEST, and it is not a screenshot against a golden
image. §39.25 adds a SECOND body to `sw_col` for the case where the mask is FF
- the byte is ours entire, so the read-modify-write pair has nothing to
preserve and becomes one store. Two bodies drawing "the same" column is
exactly the shape that goes wrong quietly: the pattern step, the row advance
and the bank wrap are duplicated, and a fast body that dropped the `xor ch, cl`
would draw a solid fill perfectly and a GREY one wrong every other row.

So the assertion is that a kernel WITH the fast body and a kernel built
NOCOLFAST=1 put identical pixels on the glass through an identical session.
Every column the session draws takes the fast body in one build and the
general body in the other, which is what makes an ordinary session - no
special shapes, no synthetic rects - the right instrument.

IT HAS A NEGATIVE CONTROL AND IT WAS RUN. Removing the `xor ch, cl` from the
fast body alone - the exact defect described above, a grey fill wrong on every
other row and a solid one perfect - makes this comparison differ in **4,992
bytes** on CGA. That is what says the fast body is actually being TAKEN by this
session: a green row from a path nothing reaches proves nothing, and
PERFORMANCE.md Part 4's rule is that the control is what tells you which of the
two kernels is lying.

WHAT IT CANNOT SEE, said plainly: it cannot see a defect in the GENERAL body,
because on an ordinary build almost nothing reaches it. A chrome fill arrives
byte-aligned (SPEC.md 11.94) and takes the fast path. `NOCOLFAST=1` is what
exercises the general one, and it is half of this test rather than a knob
nobody builds.

THE MENU BAR IS EXCLUDED. It carries a clock (SPEC.md 12.9) that advances with
wall time, so two runs minutes apart differ on rows 6..11 for reasons that have
nothing to do with the kernel under test - heapsame.py's finding, and it is the
only thing that has ever differed here.
"""
import argparse
import hashlib
import os
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty                                          # noqa: E402
import os88mouse                                          # noqa: E402
import os88sym                                            # noqa: E402
import os88geom                                           # noqa: E402
import dispcp                                             # noqa: E402


def still(m, card, tries=40, pause=0.25):
    """Wait for the framebuffer BELOW THE MENU BAR to stop changing.

    `os88marty.settle` is the usual tool and it is the wrong one here: it
    watches the WHOLE screen, and the menu bar carries a clock (SPEC.md 12.9)
    that ticks for reasons this test excludes from its own captures. On CGA
    that merely made the gate lucky; on Hercules it made it fail outright, and
    a diff of two captures 1.5 s apart said why - **6 differing pixels, rows 10
    and 12**, and nothing at all below the bar.

    So this settles on exactly the pixels the comparison is made of, which is
    the only region either build is being judged on.
    """
    prev = None
    for _ in range(tries):
        rows = m.vram(card)[2][os88geom.MBAR_H:]
        cur = b"".join(bytes(r) for r in rows)
        if cur == prev:
            return
        prev = cur
        time.sleep(pause)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--off", action="store_true",
                    help="the kernel was built NOCOLFAST=1")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--card", default="cga")
    a = ap.parse_args()
    defines = ("NOCOLFAST",) if a.off else ()
    # ...and tell the helpers that do NOT take `defines`. os88marty.no_saver
    # resolves ss_idle through os88sym directly, so on the NOCOLFAST kernel it
    # asked for a map of a different binary and the byte-identity check refused
    # - correctly, and one layer below anything this file could pass it.
    os.environ["OS88_DEFINES"] = ",".join(defines)

    def S(name):
        return os88sym.linear(name, defines)

    blob = bytearray()

    def step(m, what):
        w, h, rows = m.vram(a.card)
        px = bytes(b for r in rows[os88geom.MBAR_H:] for b in r)
        blob.extend(px)
        print("  %-22s %dx%d  %s" % (what, w, h,
                                     hashlib.md5(px).hexdigest()[:12]))

    # `boot=30` AND NOT `settle` FOR THE FIRST WAIT, which cost two runs to
    # find. SPEC.md 79's saver DRAWS, so once it is up nothing ever settles
    # again - and `settle` waits up to 120 s of HOST time, which on a guest
    # running at ~4x is long enough for the saver's idle timer to expire
    # before the desktop is even declared. On Hercules it did, every time:
    # the failure was in the BOOT gate, before there was anywhere to call
    # no_saver from. Running blind to a desktop first leaves somewhere.
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine, boot=30, card=a.card) as m:
        os88marty.no_saver(m)
        still(m, a.card)
        step(m, "desktop")

        # A window is the shape this change is for: SPEC.md 11.94 puts its
        # content origin on a multiple of 8, so its background fill, its frame
        # and every row of its chrome arrive with both edge columns WHOLE.
        dispcp.open_drive(m, mo := os88mouse.Mouse(marty=m), S,
                          lambda mm, card=None: still(mm, a.card), "B")
        step(m, "disk window")

        w = dispcp.win_list(m, S)
        wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S,
                          lambda mm, card=None: still(mm, a.card),
                          wx, wy, "APPS")
        step(m, "APPS folder")

        # ...and a menu, whose save-under and highlight are the XOR mode this
        # change does not touch - so it is here as a control rather than as a
        # case: if the two builds ever diverge HERE, the fault is not §39.25's.
        mo.menu(12, 8, 40, 45)
        still(m, a.card)
        step(m, "after a menu")

    open(a.out, "wb").write(bytes(blob))
    print("%s: %d bytes, md5 %s"
          % (a.out, len(blob), hashlib.md5(bytes(blob)).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
