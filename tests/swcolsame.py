#!/usr/bin/env python3
"""sw_col's whole-column store must change NOTHING on the glass (SPEC.md 39.25).

    python3 tests/swcolsame.py       # THE ROW: both kernels, CGA and
                                     # Hercules, compared; exit 1 on any
                                     # difference

The NOCOLFAST=1 arm is built into a PRIVATE tree (`os88build.tree`,
docs/WRITING-TESTS.md 5.2) and the shipped arm is the run's own tree, so the
row never writes build/. Hercules is the second machine because it is a
different banked layout (SPEC.md 39.3) and therefore a different wrap in the
row step this body inlines. The two-build form it replaced still works, for a
kernel somebody built by hand:

    make NOCOLFAST=1 && python3 tests/swcolsame.py sc-off.bin --off
    make             && python3 tests/swcolsame.py sc-on.bin
    cmp sc-off.bin sc-on.bin

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

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
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
    def below_bar():
        rows = m.vram(card)[2][os88geom.MBAR_H:]
        return b"".join(bytes(r) for r in rows)
    # Two identical captures `pause` apart, on the GUEST's clock; giving up
    # quietly after `tries` of them, as the host loop did.
    try:
        os88marty.quiesce(m, below_bar, guest=pause * os88marty.GUEST_PACE,
                          stable=1, budget=tries * pause * os88marty.GUEST_PACE,
                          what="the screen below the menu bar to settle")
    except os88marty.MartyError:
        pass


def session(machine, card, defines=None):
    """One scripted session on whatever kernel `build/` means right now (the
    run's tree, or a private one this process applied). Answers the blob and
    the per-step captures."""
    def S(name):
        if defines is None:
            return os88sym.linear(name)
        return os88sym.linear(name, defines)

    blob = bytearray()
    steps = []
    # dispcp's settle callback takes a `card` of its own, so the session's
    # card is bound under another name rather than shadowed.
    #
    # **THE UI FIRST, THE PIXELS SECOND.** `still` alone captured the Disk
    # window HALF-DRAWN: while the B: directory is being read nothing on the
    # screen moves, so two identical captures a guest second apart are taken
    # mid-open - an empty content box, a half-cleared one, no footer - and
    # which of those it caught varied run to run on ONE kernel (six sessions,
    # five different pictures). That made every A/B a coin toss on the one
    # step the change is about. `ui_done` waits until ui_task has dispatched,
    # drawn and finished with the drive (tools/os88marty.py), which is the
    # state; `still` then covers the emulator's own frame lag.
    sess_card = card

    def calm(mm, card=None):
        os88marty.ui_done(mm, what="the window to finish drawing")
        still(mm, sess_card)

    def step(m, what):
        w, h, rows = m.vram(card)
        px = bytes(b for r in rows[os88geom.MBAR_H:] for b in r)
        blob.extend(px)
        steps.append((what, w, px))
        print("  %-22s %dx%d  %s" % (what, w, h,
                                     hashlib.md5(px).hexdigest()[:12]))
        sys.stdout.flush()

    # `boot=30` AND NOT `settle` FOR THE FIRST WAIT, which cost two runs to
    # find. SPEC.md 79's saver DRAWS, so once it is up nothing ever settles
    # again - and `settle` waits up to 120 s of HOST time, which on a guest
    # running at ~4x is long enough for the saver's idle timer to expire
    # before the desktop is even declared. On Hercules it did, every time:
    # the failure was in the BOOT gate, before there was anywhere to call
    # no_saver from. Running blind to a desktop first leaves somewhere.
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=machine, boot=30, card=card) as m:
        os88marty.no_saver(m)
        still(m, card)
        step(m, "desktop")

        # A window is the shape this change is for: SPEC.md 11.94 puts its
        # content origin on a multiple of 8, so its background fill, its frame
        # and every row of its chrome arrive with both edge columns WHOLE.
        dispcp.open_drive(m, mo := os88mouse.Mouse(marty=m), S,
                          calm, "B")
        calm(m)
        step(m, "disk window")

        w = dispcp.win_list(m, S)
        wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, calm, wx, wy, "APPS")
        calm(m)
        step(m, "APPS folder")

        # ...and a menu, whose save-under and highlight are the XOR mode this
        # change does not touch - so it is here as a control rather than as a
        # case: if the two builds ever diverge HERE, the fault is not §39.25's.
        mo.menu(12, 8, 40, 45)
        calm(m)
        step(m, "after a menu")
    return bytes(blob), steps


MACHINES = (("os8088_5150_cga_gla", "cga"), ("os8088_5150_herc_gla", "herc"))


def ab():
    """Both kernels on both machines, and the verdict. Answers 0 or 1."""
    import os88build
    arms = (("NOCOLFAST=1", os88build.tree("NOCOLFAST=1")),
            ("shipped", os88build.plain()))
    got = {}
    for name, tree in arms:
        tree.apply()
        for machine, card in MACHINES:
            print("%s on %s" % (name, machine))
            sys.stdout.flush()
            got[name, card] = session(machine, card)[1]
    fails = []
    total = 0
    for machine, card in MACHINES:
        a, b = got["NOCOLFAST=1", card], got["shipped", card]
        if len(a) != len(b):
            fails.append("%s: the arms took %d and %d steps"
                         % (card, len(a), len(b)))
        for (what, w, p0), (_, _, p1) in zip(a, b):
            total += len(p0)
            d = [i for i in range(min(len(p0), len(p1))) if p0[i] != p1[i]]
            if d or len(p0) != len(p1):
                ys = [i // w + os88geom.MBAR_H for i in d] or [0]
                xs = [i % w for i in d] or [0]
                fails.append("%s, %s: %d of %d pixels below the menu bar "
                             "differ, rows %d..%d, columns %d..%d"
                             % (card, what, len(d), len(p0), min(ys),
                                max(ys), min(xs), max(xs)))
    print("compared %d pixels over %d machines" % (total, len(MACHINES)))
    for f in fails:
        print("FAIL " + f)
    print("swcolsame: " + ("IDENTICAL" if not fails else "*** THEY DIFFER ***"))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?",
                    help="write ONE arm's blob here, for the kernel already "
                         "built (the two-build form); without it the row "
                         "builds the knob arm and compares both itself")
    ap.add_argument("--off", action="store_true",
                    help="with OUT: the kernel was built NOCOLFAST=1")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--card", default="cga")
    a = ap.parse_args()
    if not a.out:
        return ab()
    defines = ("NOCOLFAST",) if a.off else ()
    # ...and tell the helpers that do NOT take `defines`. os88marty.no_saver
    # resolves ss_idle through os88sym directly, so on the NOCOLFAST kernel it
    # asked for a map of a different binary and the byte-identity check refused
    # - correctly, and one layer below anything this file could pass it.
    os.environ["OS88_DEFINES"] = ",".join(defines)
    os88sym.default_defines(*defines)
    blob, _ = session(a.machine, a.card, defines)
    open(a.out, "wb").write(blob)
    print("%s: %d bytes, md5 %s"
          % (a.out, len(blob), hashlib.md5(blob).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
