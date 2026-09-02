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

**IT IS CURRENTLY RED, ON PURPOSE, AND IT IS RIGHT.** Since SPEC.md 11.94.5
put a window's SIZE on the byte grid, opening this 466-wide picture leaves
`[pt_cw]` at 472: the snap rounds Paint's content width up and `pt_track`,
which defines the canvas as the content area, grows the document to match.
`pt_bmp_hdr` writes `biWidth` from `[pt_cw]`, so a Save then writes a 472-wide
file with six columns of white on the end, and nothing warns. The width
assertion below is what catches it - `make NOSIZESNAP=1` gives 466 and this
row passes every check. **Do not relax it to 472**; that blesses the defect.
docs/SAVEUNDER-LIVE-PLAN.md 28 has the measurement and the three candidate
fixes, which are a decision rather than a patch.

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
from blitpair import gif_pixels                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

S = os88sym.linear
ROWS = (0, 1, 54, 55, 108, 109)     # top, middle and bottom of the picture


def patch_caller(m, base, sym):
    """A five-byte CALL/JMP loop written over pt_blit's entry.

    The debug server will not set IP - `pc` is the FETCH pointer and a bare
    write leaves the old prefetch queue in front of the new address, which it
    says so at its own `reg_of`. So the call is made the way the 8088 makes
    one: the instruction is written where the CPU already is.

        pt_blit+0:  E8 rel16     call pt_line_get
        pt_blit+3:  EB FB        jmp pt_blit+0

    Stop on the `jmp`, read the answer, set DI, run: it loops round and calls
    again. Paint's task never leaves this until the harness quits, which is
    fine and is why the routine is the LAST thing the test does.
    """
    at = sym["pt_blit"]
    rel = (sym["pt_line_get"] - (at + 3)) & 0xFFFF
    m.write(base + at, bytes((0xE8, rel & 0xFF, rel >> 8, 0xEB, 0xFB)))
    return base + at + 3


def call_line_get(m, base, sym, spin, row, cw):
    """pt_line_get(DI=row), through the loop patch_caller left behind.

    **THIS RIG DOES NOT SURVIVE ITS SECOND CALL, AND THAT IS AN OPEN BUG IN
    THE RIG rather than in pt_line_get** (SPEC.md 42.20). Nothing had noticed
    because the row loop has not run since SPEC.md 11.94.5: the width
    assertion above it exits first, and was correctly red from that commit
    until 42.20 stopped the canvas growing into the snap's slack.

    What happens: the first call is made from a stop at `pt_blit+0` and
    returns good data - row 0 comes back matching the file exactly. Every call
    after it resumes from `spin` itself, because that is where the previous
    one stopped, and the breakpoint at `spin` is then never reported again.
    The guest is NOT hung: it spins round the patched loop for ever, which is
    why the CS:IP sampled at the timeout is a different one every run and
    never inside this package - it is whatever interrupt was in hand. The
    symptom reads as "pt_line_get(N) never came back", which points squarely
    at the routine under test.

    Ruled out: it is not the row (reordering ROWS to (54, 0, 1) fails on the
    SECOND call, whichever row that is), not `setreg` (DI is read back as the
    value written), and not the patch bytes (`e8 1f 45 eb fb` are still there
    at the stall). Stepping off the address with `advance(cycles=)` before
    re-arming does leave the guest provably elsewhere - inside pt_line_get -
    and the breakpoint at `spin` still does not fire; nor does arming
    pt_line_get's entry and `spin` alternately, which never resumes from an
    armed address at all. The remaining suspect is `patch_caller`'s own
    warning one level down: MartyPC's `pc` is the FETCH pointer, and
    `pt_blit+3` sits inside the prefetch window of the `call` at `pt_blit+0`.
    """
    m.setreg("di", row)
    m.bp_exec(spin)
    m.run()
    if not m.wait_stop(limit=120.0):
        sys.exit("paintrow: pt_line_get(%d) never came back" % row)
    return m.read(base + sym["pt_line"], cw)


def classes(seq):
    """...as a pattern: which entries match the first one."""
    first = seq[0]
    return [x == first for x in seq]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintrow.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

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
                                                              "OS8088.GIF")))
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

        # ...and now stop INSIDE Paint, so CS and DS are the package's
        m.bp_exec(base + sym["pt_blit"])
        mo.to(rx + 3, ry + 3)                   # anything that repaints it
        if not m.wait_stop(limit=120.0):
            m.bp_exec(base + sym["pt_blit"])
            mo.to(rx, ry)
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
        spin = patch_caller(m, base, sym)
        bad = 0
        for row in ROWS:
            if row >= ih:
                continue
            got = classes(call_line_get(m, base, sym, spin, row, cw))
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
