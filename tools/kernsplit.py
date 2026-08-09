#!/usr/bin/env python3
"""kernsplit.py SMALL.bin BIG.bin - what does kern_big cost over kern_small?

Two kernels off one tree (docs/KERN-SPLIT-PLAN.md). `make` builds the small
one and `make big` the big one, and this says what separates them.

WHAT IT IS FOR IS THE SMALL BUILD, not the big one. kern_big has a machine
with RAM behind it and its guard can move on its own terms; kern_small is the
128KB floor the project was written for, and the whole design rests on one
claim - that adding a feature to kern_big costs kern_small NOTHING. That claim
is only worth what it is checked against, and it fails silently: a hook left
outside its `%ifdef`, a struct sized for the maximum, a routine made
parameterised "while we are here", and the small kernel grows for a feature it
does not have. Nothing errors. The image still boots. It is simply bigger than
it needed to be, on the machine that could least afford it.

So the number to watch is the FIRST one printed, and the shape to watch for is
it moving in a commit whose subject is about kern_big.

The unit is KIMG_PARA's 512-byte rung rather than the byte count, for
fieldsize.py's reason: the memory ladder rounds, so a change that crosses no
rung costs the machine nothing YET, and one that crosses a rung costs it 512
bytes of boot and moves every heap figure. Both are reported, because they are
different questions.

It is a REPORT and never an error. kern_big is allowed to be bigger - that is
what it is for - and kern_small is allowed to grow when somebody decides it
should. What is not allowed is either happening without anybody noticing.
"""
import os
import sys


def rung(n):
    return (n + 511) // 512


def main():
    if len(sys.argv) != 3:
        print("usage: kernsplit.py SMALL.bin BIG.bin", file=sys.stderr)
        return 2
    small, big = sys.argv[1], sys.argv[2]
    for p in (small, big):
        if not os.path.exists(p):
            print("kernsplit: %s does not exist - skipping the check" % p)
            return 0

    a, b = os.path.getsize(small), os.path.getsize(big)
    ra, rb = rung(a), rung(b)

    print("kernsplit: small %6d bytes (%d sectors)" % (a, ra))
    print("kernsplit: big   %6d bytes (%d sectors)" % (b, rb))

    if a == b:
        # BYTE-FOR-BYTE, NOT SIZE-FOR-SIZE. Equal sizes prove nothing on their
        # own: the image is padded to a 512-byte rung, so a divergence of a few
        # bytes lands inside the padding and both files come out the same
        # length. Measured - an 8-byte probe behind `%ifdef KERN_BIG` moved
        # neither the byte count nor the sector count. So the comparison that
        # says "no divergence yet" has to read the bytes.
        if open(small, "rb").read() == open(big, "rb").read():
            print("kernsplit: the two builds are BYTE-IDENTICAL - no "
                  "divergence yet")
        else:
            print("kernsplit: same size, DIFFERENT BYTES - a divergence small "
                  "enough to hide in the rung's padding. It costs the machine "
                  "nothing yet and it is real.")
        return 0

    d = b - a
    print("kernsplit: big costs %+d bytes over small, %+d sector(s)"
          % (d, rb - ra))
    if rb == ra:
        print("kernsplit: same rung - the two share a memory map, so a heap "
              "figure or a boot time measured on one is comparable on the "
              "other")
    else:
        print("kernsplit: DIFFERENT rung - big's heap starts %d bytes lower "
              "and its boot reads %d more sector(s). Do not compare `boot "
              "ticks` or any heap row across the two."
              % ((rb - ra) * 512, rb - ra))
    if d < 0:
        print("kernsplit: NOTE - big is SMALLER than small, which is either a "
              "feature that has been removed from the wrong build or a stale "
              "artifact. Neither is what this knob is for.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
