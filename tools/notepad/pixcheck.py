"""Is the incrementally-drawn content the same pixels a full repaint makes?

SPEC.md 48.12's check, for Note Pad: drive the thing, capture, force a FULL
repaint of the same state, capture again, diff. The row index changed how
every walk in the module is seeded, and a seed that lands in the wrong place
draws the wrong text - which np_len, np_top and the timings cannot see.

The forced repaint is a raise: covering Note Pad and raising it again runs
W_PAINT, which fills the content and letters every row from scratch.

...EXCEPT THAT IT STOPPED BEING FORCED. SPEC.md 11.96's raise cache banks a
covered window's content and puts it back on the raise WITHOUT calling
W_PAINT, which turns the whole check into a tautology: the "full repaint" is
then a byte copy of the capture it is being compared against, so it agrees
with itself on any build, correct or not. (It was inert for as long as it has
existed - SPEC.md 50.6.1 refused every claim - so this check was honest right
up to the commit that fixed that, which is the sort of thing that only shows
up if the instrument says what it depends on.)

Two changes, and the second is the one that matters. WF_SAVEU is cleared for
the round trip, through the window record, so wm_su_take refuses and the raise
has to draw. And np_paint gets a BREAKPOINT: if it did not run, this exits
non-zero and says so rather than printing a comforting zero.
"""
import base64
import sys
import time

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import os88marty as M                                    # noqa: E402
from pkg import Lab, Labels                              # noqa: E402
from state import State                                  # noqa: E402
import drive                                             # noqa: E402

BIN = "/home/user/os8088/build/npbench.bin"
LST = "/home/user/os8088/build/npbench.lst"
DISK_BODY = (395, 135)          # blank area of the Disk window
NP_TITLE = (80, 30)             # Note Pad's title bar, left of the Disk window

KSEG = 0x0060
WIN_SIZE = 26
W_FLAGS = 0
WF_SAVEU = 32


def saveu_win(m):
    """The window record carrying WF_SAVEU - Note Pad's, and the only one."""
    base = m.sym("wm_wins")
    for i in range(12):
        p = base + i * WIN_SIZE
        f = m.read(p + W_FLAGS, 2)
        f = f[0] | (f[1] << 8)
        if (f & 1) and (f & WF_SAVEU):
            return p, f
    return None, 0


def set_saveu(m, p, on):
    f = m.read(p + W_FLAGS, 2)
    f = f[0] | (f[1] << 8)
    f = (f | WF_SAVEU) if on else (f & ~WF_SAVEU)
    m.write(p + W_FLAGS, bytes((f & 0xFF, f >> 8)))


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
    w, h, a2 = fb(m)                         # an idle screen sampled twice
    still = sum(1 for x, y in zip(before, crop(w, h, a2, *box)) if x != y)
    if still:
        print("NOT SETTLED: %d bytes moved between two idle captures - every "
              "number below is that plus whatever else" % still)

    wp, _ = saveu_win(m)
    if wp is None:
        print("no window carries WF_SAVEU - is Note Pad open?")
        return 1
    set_saveu(m, wp, False)                  # ...or the raise is served from
                                             # SPEC.md 11.96's bank and this
                                             # check compares a copy with its
                                             # own original
    np_paint = (lab.seg << 4) + Labels(LST)["np_paint"]
    drive.dclick(m, *DISK_BODY)              # cover Note Pad
    drive.wait_until(m, lambda: True, frames=200, tries=1)

    m.breakpoints([{"type": "exec", "addr": np_paint}])
    drive.goto(m, *NP_TITLE)
    m.mouse(0, 0, l=True)
    m.mouse(0, 0)
    m.step(1)
    painted = False
    for _ in range(8):
        r = m.advance(cycles=200_000_000)
        if r["state"] != "breakpoint":
            break
        painted = True
        m.step(1)
    m.breakpoints([])
    drive.wait_until(m, lambda: True, frames=400, tries=1)
    set_saveu(m, wp, True)

    w, h, b = fb(m)
    after = crop(w, h, b, *box)

    if len(before) != len(after):
        print("SIZE MISMATCH", len(before), len(after))
        return 1
    diff = sum(1 for x, y in zip(before, after) if x != y)
    print("content bytes %d, differing after a forced full repaint: %d"
          % (len(before), diff))
    # THE BBOX, ALWAYS. A count alone misattributes - the README's own example
    # is 42 pixels of "text flash" that turned out to be the mouse pointer.
    bpp = len(before) // ((box[2] - box[0] + 1) * (box[3] - box[1] + 1))
    stride = (box[2] - box[0] + 1) * bpp
    x1 = y1 = 1 << 30
    x2 = y2 = -1
    cells = set()
    for i in range(len(before)):
        if before[i] != after[i]:
            px = box[0] + (i % stride) // bpp
            py = box[1] + i // stride
            x1, y1 = min(x1, px), min(y1, py)
            x2, y2 = max(x2, px), max(y2, py)
            cells.add((px // 8, py // 8))
    if x2 >= 0:
        print("   bbox x %d..%d  y %d..%d   %d 8x8 cells: %s"
              % (x1, x2, y1, y2, len(cells),
                 sorted(cells)[:14] + (["..."] if len(cells) > 14 else [])))
    if not painted:
        print("VERDICT: INVALID - np_paint never ran, so nothing was compared "
              "against a repaint. Check the click landed on the title bar.")
        return 2
    print("VERDICT:", "incremental == full repaint" if diff == 0
          else "MISMATCH - the incremental path drew something else")
    return 0 if diff == 0 else 1


sys.exit(main())
