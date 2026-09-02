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

Three things make it forced again, and the third is the one to copy.

WF_SAVEU is cleared for the round trip, through the window record, so
wm_su_take refuses and the raise has to draw - and SPEC.md 11.96.2 makes
wm_su_ck test that flag as well, so clearing it also invalidates a cache
already banked. Without that half, this only worked when no cache happened to
be live, which is a check that passes for a reason it does not state.

np_paint gets a BREAKPOINT: if it did not run, this exits non-zero and says so
rather than printing a comforting zero.

And `--self-test` PROVES BOTH OF THOSE, in two legs: with the cache left
armed the run must come back INVALID (so the guard can see it), and with the
cache armed BEFORE the flag is cleared np_paint must run anyway (so clearing
the flag defeats a bank that already exists). That is the whole lesson of the
defect this file describes: a check reporting zero means nothing until
something has shown it can report something else. Run it after any change to
the raise path, and before believing a zero from a build you have not seen
fail.
"""
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
W_FLAGS = 0
WF_SAVEU = 32                   # SPEC.md 11.96.1. The one kernel number here
                                # that cannot be derived - the two that CAN are
                                # below, because both have moved before


def win_geom(m):
    """MAX_WIN and WIN_SIZE, out of the .bss layout rather than from memory.

    wm_wins / wm_zord / wm_zn are declared adjacent and in that order, so the
    gaps between their symbols ARE the two constants. Copied instead, they go
    stale in silence: WIN_SIZE has been 18, then 20, then 26, and a harness
    holding the old one walks the wrong records and reads plausible garbage
    (SPEC.md 12 - wm_idx2ptr open-coding the stride is what broke the first
    time it changed).
    """
    wins, zord, zn = m.sym("wm_wins"), m.sym("wm_zord"), m.sym("wm_zn")
    maxwin = zn - zord
    span = zord - wins
    if maxwin <= 0 or span <= 0 or span % maxwin:
        raise SystemExit("wm_wins/wm_zord/wm_zn are not adjacent any more "
                         "(%d, %d): derive the stride some other way"
                         % (span, maxwin))
    return maxwin, span // maxwin


def front_win(m):
    """The frontmost window's record - the one a title-bar click will raise.

    NOT "the only window carrying WF_SAVEU", which is what this used to do.
    SPEC.md 11.96.1 ends by saying a second application may now opt in, and on
    the day one does, that search picks whichever record comes first and this
    check silently measures the wrong window.
    """
    maxwin, size = win_geom(m)
    n = m.read(m.sym("wm_zn"), 1)[0]
    if not n:
        return None
    idx = m.read(m.sym("wm_zord") + n - 1, 1)[0]
    return m.sym("wm_wins") + idx * size


def get_flags(m, p):
    f = m.read(p + W_FLAGS, 2)
    return f[0] | (f[1] << 8)


def set_saveu(m, p, on):
    f = get_flags(m, p)
    f = (f | WF_SAVEU) if on else (f & ~WF_SAVEU)
    m.write(p + W_FLAGS, bytes((f & 0xFF, f >> 8)))


def cache_for(m, wp):
    """Is a raise cache banked FOR THIS WINDOW right now? (SPEC.md 11.96)

    Both words, because there is one cache for the machine and it may belong
    to somebody else - a bare `[wm_su_seg] != 0` reads another window's bank
    as ours and warns about nothing.

    AND THE TWO ADDRESSES ARE IN DIFFERENT SPACES. m.sym answers a FLAT
    address, because that is what m.read takes; [wm_su_win] is a near pointer,
    so it is KERNEL_SEG-relative. Compared raw they never match, which reads
    as "no cache was ever banked" - the self-test below reported exactly that
    on a machine that had just banked one. It is os88marty.sym's own listing
    warning in a second place: an address in the wrong space is a plausible
    number that fails quietly.
    """
    s = m.read(m.sym("wm_su_seg"), 2)
    if not (s[0] | (s[1] << 8)):
        return False
    o = m.read(m.sym("wm_su_win"), 2)
    return (o[0] | (o[1] << 8)) == wp - (KSEG << 4)


def fb(m):
    """The rendered framebuffer, through os88marty's OWN accessor.

    It used to call `cmd(cmd="fbuf")` and `base64.b64decode` the payload. The
    payload is HEX. Every hex character is also a valid base64 character, so
    the decode does not fail - it returns 4.5 bytes per pixel of nonsense
    instead of 3, deterministically. That keeps "do these two screens differ?"
    honest, because the transform is injective, and makes every byte count,
    bounding box, glyph-cell count and per-row attribution meaningless. It is
    what docs/NOTEPAD-NOTES.md 5.2.1's "66 stale cells" was measured with.
    m.fbuf() is the one decoder in the tree and is what `shot --rendered`
    uses."""
    return m.fbuf()


def crop(w, h, data, x1, y1, x2, y2):
    bpp = len(data) // (w * h)
    out = []
    for y in range(max(0, y1), min(h, y2 + 1)):
        row = data[(y * w + x1) * bpp:(y * w + x2 + 1) * bpp]
        out.append(row)
    return b"".join(out)


def round_trip(m, lab, box, force):
    """Cover Note Pad and raise it again; answer (after, painted, armed).

    `force` says WHEN WF_SAVEU is cleared, and the three values are three
    different questions:

      "before" - cleared before the cover, so wm_su_take refuses and no cache
                 is ever banked. The check's own path.
      "after"  - cleared after the cover, so a cache IS banked and only
                 wm_su_ck's flag test (SPEC.md 11.96.2) can stop it being
                 used. The self-test's second leg, and the reachable case:
                 11.91's promotion leaves a window frontmost with a live bank
                 of its own, and this check would then compare a copy with its
                 own original however early it cleared the flag.
      None     - never cleared. The self-test's control: the raise is served
                 from the bank, np_paint must NOT run.
    """
    wp = front_win(m)
    if wp is None:
        raise SystemExit("no visible window - is Note Pad open?")
    had = get_flags(m, wp) & WF_SAVEU
    if not had:
        print("NOTE: the front window does not carry WF_SAVEU, so the raise "
              "was never going to be served from the bank (SPEC.md 11.96.1)")
    if force == "before":
        set_saveu(m, wp, False)
    np_paint = (lab.seg << 4) + Labels(LST)["np_paint"]
    drive.dclick(m, *DISK_BODY)              # cover Note Pad
    drive.wait_until(m, lambda: True, frames=200, tries=1)
    armed = cache_for(m, wp)
    if force == "after":
        set_saveu(m, wp, False)

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
    if force and had:
        set_saveu(m, wp, True)               # put the app's own promise back:
                                             # every later raise in this session
                                             # is measuring the shipped path

    w, h, b = fb(m)
    return crop(w, h, b, *box), painted, armed


def self_test(m, lab, box):
    """Two legs, and neither is about Note Pad: they are about the check.

    A zero from this file means nothing until something has shown it can
    report something else - which is the whole lesson of the defect in
    SPEC.md 11.96.2, where a W_PAINT count reported success about a window
    drawing a solid black rectangle.

      leg 1  cache left armed          -> np_paint must NOT run
             The state that turned this check into a tautology. It proves the
             guard can see it, so `VERDICT: INVALID` is reachable.

      leg 2  cache armed, THEN the flag cleared -> np_paint MUST run
             Proves clearing WF_SAVEU defeats a bank that already exists, not
             only one that has not been taken yet. That is wm_su_ck's flag
             test; without it this check is sound only when no cache happens
             to be live, which is a pass for a reason it does not state.
    """
    print("--- self-test leg 1: the round trip with the cache left armed ---")
    _, painted, armed = round_trip(m, lab, box, force=None)
    if not armed:
        print("INCONCLUSIVE: no cache was banked, so the guard was never put "
              "to the question. A refused claim (SPEC.md 50.6) looks like "
              "this - check the heap, or that this kernel has 11.96 at all.")
        return 3
    if painted:
        print("SELF-TEST FAILED: the cache was armed and np_paint ran anyway. "
              "Either the raise did not use the bank, or the click missed.")
        return 4
    print("  leg 1 PASSED: cache armed, np_paint did NOT run - so an unforced "
          "repaint IS caught rather than reported as zero")

    print("--- self-test leg 2: armed first, WF_SAVEU cleared after ---")
    _, painted, armed = round_trip(m, lab, box, force="after")
    if not armed:
        print("INCONCLUSIVE: leg 2 never banked a cache either")
        return 3
    if not painted:
        print("SELF-TEST FAILED: a cache was banked and clearing WF_SAVEU did "
              "not stop it being used, so np_paint never ran. This kernel is "
              "missing SPEC.md 11.96.2's flag test in wm_su_ck, and every zero "
              "this file prints is only as trustworthy as the heap's timing.")
        return 5
    print("  leg 2 PASSED: withdrawing the promise defeated a bank already "
          "taken, so the forced repaint is forced in both orders")
    return 0


def main():
    lab = Lab()
    lab.find_pkg()
    st = State(lab, BIN)
    m = lab.m

    box = (st.w("np_tx") - 8, st.w("np_ty"), st.w("np_rgt"), st.w("np_bot"))
    print("content box x %d..%d  y %d..%d   top=%d cur=%d"
          % (box[0], box[2], box[1], box[3], st.w("np_top"), st.w("np_cur")))

    # PARK THE POINTER OUTSIDE THE CONTENT FIRST. This routine moves the
    # mouse between the two captures (it has to - it covers the window and
    # raises it again), so an arrow left inside the box is 36 pixels of
    # difference that has nothing to do with the drawing under test. The
    # README's own warning about misattribution, arriving in the instrument
    # that carries it: it read as a real mismatch and cost a diagnosis.
    drive.goto(m, NP_TITLE[0], NP_TITLE[1])
    drive.wait_until(m, lambda: True, frames=120, tries=1)

    w, h, a = fb(m)
    before = crop(w, h, a, *box)
    w, h, a2 = fb(m)                         # an idle screen sampled twice
    still = sum(1 for x, y in zip(before, crop(w, h, a2, *box)) if x != y)
    if still:
        print("NOT SETTLED: %d bytes moved between two idle captures - every "
              "number below is that plus whatever else" % still)

    if "--self-test" in sys.argv:
        return self_test(m, lab, box)

    after, painted, armed = round_trip(m, lab, box, force="before")
    if armed:
        print("note: a bank for this window was already live when the flag "
              "was cleared (SPEC.md 11.91 promotion). wm_su_ck refuses it, and "
              "`painted` below is what says so - run --self-test if in doubt.")

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


if __name__ == "__main__":      # ...and not at module scope, so another
    sys.exit(main())            # harness can `import pixcheck` for front_win,
                                # win_geom and cache_for without running a
                                # whole round trip as a side effect of the
                                # import (it did, and the traceback it printed
                                # was this file's own report)
