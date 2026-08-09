"""Is the incrementally-drawn content the same pixels a full repaint makes?

SPEC.md 48.12's check, for Note Pad: drive the thing, capture, force a FULL
repaint of the same state, capture again, diff. The row index changed how
every walk in the module is seeded, and a seed that lands in the wrong place
draws the wrong text - which np_len, np_top and the timings cannot see.

The forced repaint is a raise: covering Note Pad and raising it again runs
W_PAINT, which fills the content and letters every row from scratch.
"""
import base64
import sys
import time

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import os88marty as M                                    # noqa: E402
from pkg import Lab                                      # noqa: E402
from state import State                                  # noqa: E402
import drive                                             # noqa: E402

BIN = "/home/user/os8088/build/npbench.bin"
DISK_BODY = (395, 135)          # blank area of the Disk window
NP_TITLE = (80, 30)             # Note Pad's title bar, left of the Disk window


def fb(m):
    r = m.cmd(cmd="fbuf", rendered=True)
    return r["w"], r["h"], base64.b64decode(r["data"])


def crop(w, h, data, x1, y1, x2, y2):
    bpp = len(data) // (w * h)
    out = []
    for y in range(max(0, y1), min(h, y2 + 1)):
        row = data[(y * w + x1) * bpp:(y * w + x2 + 1) * bpp]
        out.append(row)
    return b"".join(out)


def main():
    lab = Lab()
    lab.find_pkg()
    st = State(lab, BIN)
    m = lab.m

    box = (st.w("np_tx") - 8, st.w("np_ty"), st.w("np_rgt"), st.w("np_bot"))
    print("content box x %d..%d  y %d..%d   top=%d cur=%d"
          % (box[0], box[2], box[1], box[3], st.w("np_top"), st.w("np_cur")))

    w, h, a = fb(m)
    before = crop(w, h, a, *box)

    drive.dclick(m, *DISK_BODY)              # cover Note Pad
    drive.wait_until(m, lambda: True, frames=200, tries=1)
    drive.click(m, *NP_TITLE, frames=200)    # ...and raise it: a full W_PAINT
    drive.wait_until(m, lambda: True, frames=400, tries=1)

    w, h, b = fb(m)
    after = crop(w, h, b, *box)

    if len(before) != len(after):
        print("SIZE MISMATCH", len(before), len(after))
        return 1
    diff = sum(1 for x, y in zip(before, after) if x != y)
    print("content bytes %d, differing after a forced full repaint: %d"
          % (len(before), diff))
    print("VERDICT:", "incremental == full repaint" if diff == 0
          else "MISMATCH - the incremental path drew something else")
    return 0 if diff == 0 else 1


sys.exit(main())
