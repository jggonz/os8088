#!/usr/bin/env python3
"""IS pt_line_get STILL THE ROW? (SPEC.md 42.13.1.2)

    make && python3 tests/paintrow.py

`pt_line_get` unpacks one canvas row into one colour index per column, and on
a colour adapter that row is FOUR PLANES - eight pixels have to be gathered a
bit at a time out of four bytes. **Its only caller is the GIF writer**, one
row per pass of the LZW loop, which is why it was the last planar routine with
no test at all: nothing that draws goes near it, so every screenshot in this
directory passes with it broken.

That mattered the moment its inner loop was unrolled to take the bounds test
and the counter off a pixel that is otherwise eight shifts (SPEC.md 42.13.1.2)
- a fast path for a whole byte column plus a general one for the last, partial
column of a row. Unrolling a loop and leaving its exit condition behind is the
classic way to write a routine that is right for 63 columns of 59.

**THE WIDTH ASSERTION IS HALF OF IT, and it was red for a month.** SPEC.md
11.94.5 put a window's SIZE on the byte grid, so opening this 466-wide picture
left `[pt_cw]` at 472: the snap rounded Paint's content width up and pt_track,
which defined the canvas AS the content area, grew the document to match.
`pt_bmp_hdr` writes `biWidth` from `[pt_cw]`, so a Save wrote a 472-wide file
with six columns of white welded to the end and nothing warned. SPEC.md 42.20
is the fix and this row is what held the line until it arrived - do not relax
it to 472, which would bless the defect rather than catch it.

**And the row loop under it had never run at all**, because the width
assertion exits first. When it finally did, it did not work either - for
reasons that were entirely the rig's and are written down at patch_caller and
call_line_get. pt_line_get itself was right the whole time.

**IT CALLS THE ROUTINE, rather than driving something that calls it.** Paint
is stopped at `pt_blit`'s entry, so CS and DS are already the package's, and
five bytes written over that entry - `call pt_line_get` / `jmp` back to it -
make a loop the harness steps round one row at a time. The debug server will
not set IP and says why at its own `reg_of`, so the call is made the way the
8088 makes one: by putting the instruction where the CPU already is. That is
the whole rig, and it is what makes a routine with one UI-deep caller
testable in ten lines instead of a Save As dialog.

The comparison is a PATTERN, not indices: Paint maps a GIF's colours onto its
own sixteen (`pt_map16`), so the value in `pt_line` is not the value in the
file. Two colours in, two classes out - so the row is reduced to "same as
column 0 or not" on both sides and those are compared. Both classes have to
appear, or a row of one colour would pass with the routine returning zeros.

OS8088.GIF is 466 wide, which is 58 whole byte columns and a remainder of 2,
so every row tests the fast path 58 times and the partial path once.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
import dispapps                                              # noqa: E402
from blitpair import gif_pixels                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

S = os88sym.linear
ROWS = (0, 1, 54, 55, 108, 109)     # top, middle and bottom of the picture


def _diskname(path):
    """What os88disk called it: colour_gif already answers in 8.3 upper case."""
    return os.path.basename(path)


def patch_caller(m, base, sym):
    """An eight-byte loop written over pt_blit's entry, CARRYING THE ROW.

        pt_blit+0:  BF row       mov di, <row>
        pt_blit+3:  E8 rel16     call pt_line_get
        pt_blit+6:  EB F8        jmp pt_blit+0

    The debug server will not set IP - `pc` is the FETCH pointer and a bare
    write leaves the old prefetch queue in front of the new address, which it
    says so at its own `reg_of`. So the call is made the way the 8088 makes
    one: the instruction is written where the CPU already is. Paint's task
    never leaves this until the harness quits, which is fine and is why the
    routine is the LAST thing the test does.

    **THE ROW IS AN IMMEDIATE AND NOT A REGISTER**, which is what makes the
    loop drivable at all. Two things this rig used to do cannot be done here:

    - `setreg di` between turns does not stick. pt_line_get pushes DI at entry
      and pops it at exit, so a value written while the guest is inside the
      routine is thrown away by its own epilogue and the loop goes on
      redrawing the row it already had.
    - a breakpoint cannot be used to find a moment when it WOULD stick.
      Measured: the first exec breakpoint after a fresh run fires, and no
      breakpoint armed after that stop ever fires again - not at the same
      address, not at pt_line_get's entry, not at its exit 330 bytes along -
      while `run` is provably resuming (127 million cycles in eight seconds,
      state still 'running'). The rig read that as "pt_line_get(N) never came
      back", which points squarely at the routine under test.

    Writing the immediate needs neither a breakpoint nor a register, so the
    loop is driven by one memory write and a timed advance.

    Returns the address of the immediate.
    """
    at = sym["pt_blit"]
    rel = (sym["pt_line_get"] - (at + 6)) & 0xFFFF
    m.write(base + at, bytes((0xBF, 0, 0, 0xE8, rel & 0xFF, rel >> 8,
                              0xEB, 0xF8)))
    return base + at + 1

# pt_line_get is about 19,000 cycles for a 466-column planar row, so this is
# roughly ten turns of the loop. It does not have to land BETWEEN turns: the
# loop redraws the same row every time, so once one complete pass has run,
# every later pass writes pt_line the identical bytes. That idempotence is
# what replaces the breakpoint the debugger will not give (see patch_caller).
TURNS = 200000


def call_line_get(m, base, sym, rowat, row, cw):
    """pt_line_get(<row>), through the loop patch_caller left behind.

    **SETTLED, not timed.** Two consecutive reads that agree is the proof that
    the loop has been round at least once on THIS row, and it is needed twice
    over. The 8088 has a prefetch queue and the patch is written while the CPU
    is stopped at the address being patched, so the first turn executes the
    bytes that were queued before it - the real pt_blit, which then blits the
    whole canvas and calls pt_line_get for every row of it, leaving pt_line
    holding one this call never asked for. And a canvas blit is millions of
    cycles, so no fixed advance is safely bigger than it.

    That is what made row 0 - and only row 0 - come back wrong: every later
    row was read from a loop that had long since taken over.
    """
    m.write(rowat, bytes((row & 0xFF, (row >> 8) & 0xFF)))
    prev = None
    for _ in range(40):
        m.advance(cycles=TURNS)
        cur = m.read(base + sym["pt_line"], cw)
        if cur == prev:
            return cur
        prev = cur
    sys.exit("paintrow: pt_line never settled for row %d - the patched loop "
             "is not running, or is not running this row" % row)

def classes(seq):
    """...as a pattern: which entries match the first one."""
    first = seq[0]
    return [x == first for x in seq]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintrow.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default=None,
                    help="the picture to open (default: a COLOUR derivation "
                         "of build/OS8088.GIF - see below)")
    a = ap.parse_args()

    # A COLOUR PICTURE, and it has to be said now. SPEC.md 42.23.6 opens a GIF
    # whose colour table has two entries ONE BIT DEEP on any adapter, and
    # build/OS8088.GIF has exactly two - so the fixture every other paint row
    # uses stopped being able to give THIS one a four-plane canvas at all, and
    # the row failed saying "no gfx_blitp", which is true and points at the
    # wrong thing. dispapps.colour_gif appends two unused entries and changes
    # not one pixel, so `gif_pixels` below is the oracle it always was.
    if a.gif is None:
        a.gif = dispapps.colour_gif()

    if a.apps == "/tmp/paintrow.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + a.gif)

    iw, ih, px = gif_pixels(a.gif)
    sym = pkg_syms("apps/paint/paint.asm")
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, bx, by, "MEDIA")
        settle(m)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, settle, bx, by,
                                                dispcp.row_of(m, S,
                                                              _diskname(a.gif))))
        mo.to(rx, ry)
        settle(m)
        # the FIRST canvas blit is what tells us where the package landed, and
        # it is a kernel call from Paint's task, so its return address is one
        m.bp_exec("gfx_blitp")
        mo.dblclick(rx, ry)
        if not m.wait_stop(limit=300.0):
            sys.exit("paintrow: no gfx_blitp - the canvas is not planar, so "
                     "there is no four-plane row to unpack")
        r = m.regs()
        base = int.from_bytes(
            m.read((r["ss"] << 4) + r["sp"] + 2, 2), "little") << 4
        m.bp_exec()
        m.run()
        time.sleep(6)

        # ...and now stop INSIDE Paint, so CS and DS are the package's.
        #
        # **A DRAG OF PAINT'S OWN TITLE BAR, and it has to be** - this was
        # `mo.to(rx + 3, ry + 3)`, commented "anything that repaints it", and a
        # POINTER MOVE REPAINTS NOTHING. Measured on this tree and on upstream
        # `main`, three targets each: rx+3/ry+3, rx/ry and 4,4 all fail to
        # reach pt_blit inside 25 s, and a title drag hits it every time. The
        # reason is on the glass: the file row is at (164,145) and Paint opens
        # at (71,24) 522x152 over it, so the move lands on Paint's OWN palette
        # strip - and the arrow is a save-under (SPEC.md 7.1), so crossing a
        # window does not ask it to draw. The row has been failing on this
        # step for as long as the geometry has been this shape; it is not a
        # regression in anything.
        #
        # A drag is the right provoker rather than a lucky one: wm_drag
        # damages what the window vacates and W_PAINT is Paint's first
        # pt_blit caller (SPEC.md 42). Small, so the window stays on screen,
        # and it does not matter that the drag never completes - the
        # breakpoint stops the machine inside it, which is exactly where the
        # patch below wants to be.
        wx, wy, ww = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:3]
        m.bp_exec(base + sym["pt_blit"])
        mo.drag(wx + ww // 2, wy + 4, wx + ww // 2 - 40, wy + 30)
        if not m.wait_stop(limit=120.0):
            sys.exit("paintrow: Paint never repainted its canvas, so "
                     "there is nowhere to call the routine from")
        cw = m.read(base + sym["pt_cw"], 2)
        cw = cw[0] | (cw[1] << 8)
        planar = m.read(base + sym["pt_planar"], 1)[0]
        print("   canvas %d wide, [pt_planar] = %d, package at %05X"
              % (cw, planar, base))
        if not planar or cw != iw:
            sys.exit("paintrow: the canvas is %d wide and planar=%d - the "
                     "picture did not open as itself" % (cw, planar))
        if m.regs()["ip"] != sym["pt_blit"]:
            sys.exit("paintrow: stopped at IP %04X, not pt_blit's entry - the "
                     "patch below would land mid-instruction"
                     % m.regs()["ip"])
        rowat = patch_caller(m, base, sym)
        bad = 0
        for row in ROWS:
            if row >= ih:
                continue
            got = classes(call_line_get(m, base, sym, rowat, row, cw))
            want = classes(px[row * iw:(row + 1) * iw])
            if len(set(want)) != 2:
                sys.exit("paintrow: file row %d is one colour, so it proves "
                         "nothing - pick another" % row)
            if len(set(got)) != 2:
                sys.exit("paintrow: pt_line_get(%d) came back all one value, "
                         "so the row was never unpacked" % row)
            n = sum(1 for g, w in zip(got, want) if g != w)
            print("   row %3d: %d of %d columns differ" % (row, n, iw))
            bad += n
        m.bp_exec()                     # ...and Paint stays in the loop: its
    if bad:                             # entry point is five bytes of ours
        sys.exit("paintrow: FAILED - %d columns came back as the wrong "
                 "colour class" % bad)
    print("paintrow: the four-plane row reader is still the row, whole byte "
          "columns and the partial one")
    return 0


if __name__ == "__main__":
    sys.exit(main())
