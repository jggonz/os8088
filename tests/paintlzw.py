#!/usr/bin/env python3
"""WHERE DOES A GIF OPEN SPEND ITS TIME? - guest cycles, phase by phase.

    make && python3 tests/paintlzw.py

SPEC.md 42.21. `tests/paintgif.py` asks how long the whole operation takes -
launch, decode and draw together, off the association - which is the number
the field reported and the right one to watch for a regression that could be
anywhere. It cannot say WHICH HALF, and the two halves of a decode have
nothing to do with each other: `pt_gdec` is LZW and `pt_line_put` is the
four-plane transpose every picture goes through whatever wrote it.

So this one brackets the inside. Exec breakpoints on Paint's OWN labels split
`pt_gif_in` into the header parse, the sub-block flatten, the canvas claim and
the decode; then, inside the decode, a breakpoint on `pt_line_put` and one on
the `pt_gnrow` that follows it separate the row packing from the LZW loop that
fed it. Both numbers are per pixel, which is the only form in which they can
be compared with each other or with the next picture.

**That split is the whole finding.** The decode was 12,547 ms on a 4.77 MHz
8088 and it was NOT evenly shared: 999 cycles a pixel in the LZW loop against
169 in the packer, because the reader emitted one pixel per near call through
`pt_gemit` with two more calls around it for the character stack. SPEC.md
42.21 fills the string forwards instead and moves it with `rep movsb`; SPEC.md
42.13.1.4 then straightens out the byte column the packer was fetching four
bytes of loop control for. Neither could have been aimed without this row, and
the second would have been aimed WRONG - the packer looks like the expensive
half in a profile that cannot see inside `pt_gdec`.

RUN IT ON BOTH CANVAS FORMATS. `--machine os8088_5150_cga_gla` puts the canvas
on packed nibbles, which is a different loop in `pt_line_put` and was the
worse of the two (185 cycles a pixel against the planar 169). The LZW half is
adapter-independent and reads the same on either, which is itself worth
checking: 205 on a VGA and 206 on a CGA.

CYCLES, not wall clock, for tests/paintgif.py's reason: MartyPC runs the guest
at whatever multiple of real time the host manages, so a stopwatch here times
the host. Guest cycles are seconds/4,772,727 and are exactly reproducible.

PAINT'S LOAD SEGMENT COMES OFF THE STACK, which is the only part of the rig
worth knowing in general. `OSAPI_SLOT` is `push ds` / `push cs` / `pop ds` /
`call <routine>`, so at the kernel routine's entry `[SS:SP]` is the near
return into the slot and `[SS:SP+2]` is the CALLER'S DS - the package
segment. Paint says `Decoding GIF` through `OSAPI_TOAST` one instruction
before it calls `pt_gif_in` (SPEC.md 42.14), so one breakpoint on
`toast_show` hands over the segment AND the position, with no window to
race and no second run to correlate against.

The ceilings are DELIBERATELY LOOSE. They exist to catch a return to the old
shape - three calls and a palette lookup a pixel, or a rolled byte column -
and not to pin a figure that a picture with a different dither would move.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                  # noqa: E402
import dispapps                                              # noqa: E402
from paintmove import pkg_syms                                # noqa: E402

S = os88sym.linear
MHZ = 4772727.0

LZW_MAX = 400.0                 # cycles a pixel; it was 999 and is now 205
PUT_MAX = 150.0                 # ...and 169 planar, now 125 (packed: 185 -> 49)
TOTAL_MAX = 6000.0              # ms for the whole pt_gif_in; it was 12,547
ROWS = 24                       # rows sampled for the packer's share


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def paint_base(m, mo, rx, ry):
    """Double-click the picture and answer where Paint landed."""
    m.bp_exec("toast_show")
    mo.dblclick(rx, ry)
    for _ in range(12):
        if not m.wait_stop(limit=180.0):
            sys.exit("paintlzw: no toast_show - Paint never launched")
        r = m.regs()
        seg = u16(m.read((r["ss"] << 4) + r["sp"] + 2, 2))
        txt = bytes(m.read((r["es"] << 4) + r["si"], 24)).split(b"\0")[0]
        if txt.startswith(b"Decoding"):
            return seg << 4, txt
        m.run()
    sys.exit("paintlzw: Paint never said 'Decoding' - SPEC.md 42.14 is gone")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintlzw.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default=None)
    a = ap.parse_args()

    # A COLOUR PICTURE, and it has to be said now: SPEC.md 42.23.6 opens a GIF
    # whose colour table has two entries ONE BIT DEEP on any adapter, and
    # build/OS8088.GIF has exactly two - so the fixture every picture row here
    # uses stopped being able to give this one a four-plane canvas.
    # dispapps.colour_gif appends two unused entries and changes not one
    # pixel, so every oracle below is the one it always was.
    gif = dispapps.colour_gif()
    gifname = os.path.basename(gif)

    sym = pkg_syms("apps/paint/paint.asm")
    # The docstring's recipe, RUN rather than transcribed (tests/paintgif.py's
    # rule): a disk built by hand is a row that passes for whoever built it.
    os88marty.scratch_disk(a.apps, "APPS:build/paint.o88", "MEDIA:" + gif)
    name = os.path.basename(gif).upper()

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "MEDIA")
        os88marty.settle(m)

        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S, name)))
        mo.to(rx, ry)
        os88marty.settle(m)

        base, txt = paint_base(m, mo, rx, ry)
        print("   paint at segment %04x, toast %r" % (base >> 4, txt))

        def to(*names):
            m.bp_exec(*[base + sym[n] for n in names])
            m.run()
            if not m.wait_stop(limit=600.0):
                sys.exit("paintlzw: never reached %s" % (names,))
            return m.status()["cycles"]

        marks = [("pt_gif_in", to("pt_gif_in")),
                 ("pt_gdeblk", to("pt_gdeblk")),
                 ("pt_adopt", to("pt_adopt")),
                 ("pt_gdec", to("pt_gdec"))]

        # --- inside the decode: the packer against the loop that feeds it.
        # A row is put -> next put, and pt_gnrow is the instruction after
        # pt_line_put returns, so the difference is the packer alone.
        puts, rows, prev = [], [], None
        for _ in range(ROWS):
            t1 = to("pt_line_put")
            t3 = to("pt_gnrow")
            puts.append(t3 - t1)
            if prev is not None:
                rows.append(t1 - prev)
            prev = t1
        marks.append(("done", to("pt_wfollow")))

        gw = u16(m.read(base + sym["pt_gw"], 2))
        gh = u16(m.read(base + sym["pt_gh"], 2))
        cw = u16(m.read(base + sym["pt_cw"], 2))
        m.bp_exec()
        m.run()

    print("   picture %dx%d into a %d-wide canvas" % (gw, gh, cw))
    print()
    print("   %-26s %12s %9s" % ("phase", "cycles", "ms"))
    for (l0, c0), (l1, c1) in zip(marks, marks[1:]):
        print("   %-26s %12d %9.1f"
              % ("%s -> %s" % (l0, l1), c1 - c0, (c1 - c0) * 1000.0 / MHZ))
    total = (marks[-1][1] - marks[0][1]) * 1000.0 / MHZ
    print("   %-26s %12s %9.1f" % ("pt_gif_in TOTAL", "", total))

    put = sum(puts) / float(len(puts)) / cw
    lzw = (sum(rows) / float(len(rows)) - sum(puts) / float(len(puts))) / cw
    print()
    print("   LZW loop        %7.1f cycles/pixel  (ceiling %.0f)"
          % (lzw, LZW_MAX))
    print("   pt_line_put     %7.1f cycles/pixel  (ceiling %.0f)"
          % (put, PUT_MAX))

    bad = []
    if lzw > LZW_MAX:
        bad.append("the LZW loop is %.1f cycles a pixel, over %.0f - SPEC.md "
                   "42.21's run emitter is gone or bypassed" % (lzw, LZW_MAX))
    if put > PUT_MAX:
        bad.append("pt_line_put is %.1f cycles a pixel, over %.0f - SPEC.md "
                   "42.13.1.4's straight-line byte column is rolled again"
                   % (put, PUT_MAX))
    if total > TOTAL_MAX:
        bad.append("pt_gif_in is %.0f ms, over %.0f" % (total, TOTAL_MAX))
    for b in bad:
        print("paintlzw: " + b)
    if bad:
        return 1
    print("paintlzw: %dx%d decodes in %.0f ms of 4.77MHz 8088" % (gw, gh, total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
