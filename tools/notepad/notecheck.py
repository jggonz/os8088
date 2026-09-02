"""Read Note Pad's document claim out of guest RAM and compare it with the
file it was loaded from, with whatever edits we have made applied on the host.

This is the check the rep movsb change actually needs: a move that is fast and
wrong is the worst available outcome, and neither the screen nor np_len can
see a byte that landed in the wrong place.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pkg import Lab                                     # noqa: E402
from state import State                                 # noqa: E402

BIN = "/home/user/os8088/build/npbench.bin"
SRC = "/home/user/os8088/build/readme.txt"


def note(lab, st):
    seg = st.w("np_dseg")
    n = st.w("np_len")
    return lab.m.readseg(seg, 0, n), n


def expected(edits):
    raw = open(SRC, "rb").read().replace(b"\r\n", b"\r")
    for at, ch in edits:
        raw = raw[:at] + ch + raw[at:]
    return raw


if __name__ == "__main__":
    lab = Lab()
    lab.find_pkg()
    st = State(lab, BIN)
    # edits given as at:char pairs on the command line, applied left to right
    edits = []
    for a in sys.argv[1:]:
        at, ch = a.split(":")
        edits.append((int(at), ch.encode()))
    got, n = note(lab, st)
    want = expected(edits)
    print("guest note %d bytes, expected %d" % (n, len(want)))
    if n != len(want):
        print("LENGTH MISMATCH")
    lim = min(n, len(want))
    bad = [i for i in range(lim) if got[i] != want[i]]
    print("differing bytes in the common %d: %d" % (lim, len(bad)))
    if bad:
        i = bad[0]
        print("  first at %d: guest %r expected %r" % (i, got[i:i+24], want[i:i+24]))
    print("VERDICT:", "buffer is byte-exact" if not bad and n == len(want)
          else "CORRUPT")
