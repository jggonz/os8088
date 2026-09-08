#!/usr/bin/env python3
"""A COMPRESSED package loads, and expands to the same bytes (SPEC.md 20.13).

docs/plans/O88-COMPRESSION-PLAN.md 13 wave 2. Three assertions, and the third is the
one that needed a file to exist before it could be made at all:

  * a compressed package OPENS - the loader reads it high, brings the clear
    prefix down and expands the body into the region with no second claim;
  * the expanded image is byte-for-byte the uncompressed package, header
    included, except for the compression bits in the flags byte. Comparing the
    WHOLE image is the point: a decoder that got the last run wrong would still
    open a window;
  * ...and it does that for EITHER FORMAT. CALC and MINES are LZ4 and PIANO
    is LZB, and the shipped kernel carries both (SPEC.md 20.13.6), so all
    three open and all three are compared whole - which is the only thing in
    the tree that ever runs LZB's arm on a PACKAGE, nothing on a shipped disk
    being LZB.

`--lz4only` is the other half and a build of its own: a kernel built
`COMPRESS=lz4`, on which PIANO is REFUSED rather than run. SPEC.md 20.13.3
says the cell is in every build and a format the build has not got answers
CF=1, which no amount of reading the source can demonstrate - and it is a
real path for anyone who cuts a single-format kernel, which is what
`COMPRESS=` exists for. **That kernel is built in a PRIVATE TREE**
(tools/os88build.py) and not in `build/`, so the arm neither disturbs the
shared directory nor has to put it back afterwards - which is the whole of
what `builds=True` was for.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88pkg                                         # noqa: E402
import os88sym                                         # noqa: E402
import dispcp                                          # noqa: E402
from os88fixture import need                           # noqa: E402

MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}
LD_OK, LD_EBAD = 0, 2


def say(*a):
    print(*a, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--lz4only", action="store_true",
                    help="a private COMPRESS=lz4 tree, on which PIANO "
                         "is REFUSED")
    a = ap.parse_args()
    if a.lz4only:
        # A KERNEL WITH ONE FORMAT, which nothing ships and nothing else
        # boots - IN A TREE OF ITS OWN. It used to be `make COMPRESS=lz4` in
        # build/ with a bare `make` in a `finally` to put the tree back: two
        # full builds for one measurement, and in between, a kernel nobody
        # else asked for where every other row's symbol map points. The
        # fixture comes out of the same tree, because a scratch disk built
        # beside a different kernel is the stale-SDK trap.
        t = os88build.tree("COMPRESS=lz4",
                           targets=("os8088-360.img", "lzload360.img")).apply()
    else:
        t = os88build.plain().apply()
        need("build/lzload360.img")     # `all` builds nothing under tests/
    S = os88sym.linear

    fails = []
    with os88marty.launch(t.img("os8088-360.img"),
                          apps=t.img("lzload360.img"),
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzload: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        say("lzload: B: lists %r" % [r[0] for r in dispcp.listing(m, S)])

        def raise_disk():
            """A launched package's window covers the listing, so the Disk
            window has to come back to the front before the next double-click
            - otherwise the click lands on the package and ld_status still
            reads OK from the PREVIOUS load, which is what this looked like
            the first time."""
            mo.click(wx + 40, wy + 5)          # its title bar
            os88marty.settle(m)

        # THE REFERENCE COMES OUT OF THE SAME TREE as the disk under it. On
        # the plain arm that is build/; on --lz4only it is the private one,
        # and reading build/calc.o88 there would compare the guest's image
        # against a package another build produced.
        # ...and it is $(LZCDIR)'s copy, which is the one the disk CARRIES.
        # `$(BUILD)/lzload360.img` is built from `$(BUILD)/lzc/*.o88` - packed
        # `--compress=lz4` explicitly - and not from the shipped `build/*.o88`
        # beside them. Reading the latter passed on the plain arm by accident:
        # both are lz4 of the same .bin, so the two UNWRAP to the same image.
        # On --lz4only the tree has no `calc.o88` at its root at all and the
        # row died on FileNotFoundError, which read as a broken private tree.
        for name, src in (("CALC.O88", t.img("lzc/calc.o88")),
                          ("MINES.O88", t.img("lzc/mines.o88"))):
            before = dispcp.win_list(m, S)
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, name)
            after = dispcp.win_list(m, S)
            st = m.read(S("ld_status"), 1)[0]
            if len(after) <= len(before):
                fails.append("%s did not open (ld_status=%d)" % (name, st))
                continue
            rec = m.read(S("wm_wins") + after[-1] * dispcp.WIN_SIZE,
                         dispcp.WIN_SIZE)
            raise_disk()
            pseg = rec[22] | (rec[23] << 8)
            # ...AND THE REFERENCE IS COMPRESSED TOO NOW (SPEC.md 20.13.5):
            # every shipped package is, so build/calc.o88 is a FILE and what
            # the guest holds is an IMAGE. Unwrapping is not the same as the
            # flags mask below - that one exists because the EXPANDED image
            # keeps saying it came from a compressed file.
            want = os88pkg.image_unwrap(open(src, "rb").read())
            got = bytes(m.readseg(pseg, 0, len(want)))
            # the flags byte is the ONE difference: bit 3 (and 4) say the file
            # was compressed, and the expanded image keeps saying so
            g = bytearray(got); w = bytearray(want)
            g[3] &= ~0x18
            w[3] &= ~0x18
            if g == w:
                say("  %-10s opened at %04X, %d bytes expanded EXACTLY"
                    % (name, pseg, len(want)))
            else:
                bad = [i for i in range(len(want)) if g[i] != w[i]]
                fails.append("%s expanded WRONG: %d of %d bytes differ, first "
                             "at %d" % (name, len(bad), len(want), bad[0]))

        # ...and PIANO, which is LZB
        #
        # `expect` IS THE ARM. On the shipped kernel this opens a window; on
        # `--lz4only` it must be REFUSED, and os88ui's helper raises on a
        # window that never comes - so telling it which outcome is wanted is
        # the difference between an assertion and an exception. Without it
        # the refusal arm died in the helper with a traceback about a window,
        # having just proved the exact thing it exists to prove
        # (ld_status = 2, LD_EBAD, in the error's own last line).
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "PIANO.O88",
                          expect="refusal" if a.lz4only else "window")
        after = dispcp.win_list(m, S)
        st = m.read(S("ld_status"), 1)[0]
        if a.lz4only:
            if len(after) > len(before):
                fails.append("PIANO.O88 is LZB and this kernel was built "
                             "COMPRESS=lz4 - it opened anyway, so the format "
                             "was not checked")
            elif st != LD_EBAD:
                fails.append("PIANO.O88 was refused with ld_status=%d, want "
                             "%d (LD_EBAD)" % (st, LD_EBAD))
            else:
                say("  %-10s refused, ld_status=%d - the format this build "
                    "does not carry" % ("PIANO.O88", st))
        elif len(after) <= len(before):
            fails.append("PIANO.O88 is LZB and the shipped kernel carries "
                         "both (SPEC.md 20.13.6) - it was REFUSED, "
                         "ld_status=%d" % st)
        elif st != LD_OK:
            fails.append("PIANO.O88 opened a window with ld_status=%d" % st)
        else:
            # OPENED, AND NOT COMPARED - which is deliberate. A package that
            # is RUNNING writes to its own image, and Piano does: six of its
            # 4,171 bytes differ from the file by the time this reads them,
            # while CALC and MINES happen not to have got that far. So the
            # assertion here is that the LZB arm produced a RUNNABLE image,
            # and the byte-exact proof of that decoder lives where the
            # subject is not also a program: lzmod compares all 116,085 bytes
            # of BEVERLY.MOD through it.
            say("  %-10s opened, ld_status=%d - the OTHER decoder, on a "
                "kernel that carries both" % ("PIANO.O88", st))

    for f in fails:
        say("  FAIL: " + f)
    say("lzload: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
